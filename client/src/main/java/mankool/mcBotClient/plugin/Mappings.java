package mankool.mcBotClient.plugin;

import java.io.BufferedReader;
import java.io.IOException;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.function.Function;
import java.util.regex.Pattern;

/**
 * Mojang's names against the ones the game runs under, joined through the official (obfuscated)
 * names both sides know: Mojang's ProGuard file gives Mojang -> official, {@link Official} gives
 * official -> runtime. Internal names (slashes) throughout.
 *
 * <p>The runtime mappings name a method only where it is first declared, and the game's own jar
 * carries that name down to every override: Entity's getBoundingBox overrides an interface's, so
 * it is found up the supertypes, which {@code runtimeSupers} gives by runtime name.
 */
final class Mappings {

    /** Official names to the ones the game runs under; a name it does not know comes back as is. */
    interface Official {
        String cls(String internalName);

        String method(String owner, String name, String descriptor);

        String field(String owner, String name, String descriptor);
    }

    // Mojang -> runtime.
    final Map<String, String> classes = new HashMap<>();
    // Mojang owner -> name + Mojang descriptor -> runtime name; fields by name alone.
    final Map<String, Map<String, String>> methods = new HashMap<>();
    final Map<String, Map<String, String>> fields = new HashMap<>();
    // Runtime -> Mojang, for renaming the game's own jars to compile against.
    final Map<String, String> classesBack = new HashMap<>();
    final Map<String, Map<String, String>> methodsBack = new HashMap<>();
    final Map<String, Map<String, String>> fieldsBack = new HashMap<>();

    private static final Pattern LINE_NUMBERS = Pattern.compile("^\\d+:\\d+:");

    private record Member(String owner, String line) {}

    static Mappings read(BufferedReader proguard, Official official, Function<String, String[]> runtimeSupers)
            throws IOException {
        Mappings m = new Mappings();
        // Every class first: a member's descriptor names classes declared further down the file.
        Map<String, String> toOfficial = new HashMap<>();
        List<Member> members = new ArrayList<>();
        String owner = null;
        for (String line; (line = proguard.readLine()) != null; ) {
            if (line.isEmpty() || line.startsWith("#")) {
                continue;
            }
            if (!line.startsWith(" ")) {
                int arrow = line.indexOf(" -> ");
                String named = line.substring(0, arrow).replace('.', '/');
                String obf = line.substring(arrow + 4, line.length() - 1).replace('.', '/');
                toOfficial.put(named, obf);
                owner = named;
            } else if (owner != null && !line.strip().startsWith("#")) {
                members.add(new Member(owner, line.strip()));
            }
        }
        Map<String, String> runtimeToOfficial = new HashMap<>();
        toOfficial.forEach((named, obf) -> {
            String runtime = official.cls(obf);
            m.classes.put(named, runtime);
            m.classesBack.put(runtime, named);
            runtimeToOfficial.put(runtime, obf);
        });
        Function<String, String> obfName = name -> toOfficial.getOrDefault(name, name);
        Function<String, String> runtimeName = name -> m.classes.getOrDefault(name, name);
        for (Member member : members) {
            int arrow = member.line.lastIndexOf(" -> ");
            String obf = member.line.substring(arrow + 4);
            String left = LINE_NUMBERS.matcher(member.line.substring(0, arrow)).replaceFirst("");
            int space = left.indexOf(' ');
            String type = left.substring(0, space);
            String rest = left.substring(space + 1);
            String ownerObf = toOfficial.get(member.owner);
            String ownerRuntime = m.classes.get(member.owner);
            int paren = rest.indexOf('(');
            if (paren < 0) {
                String runtime = official.field(ownerObf, obf, descriptor(type, obfName));
                m.fields.computeIfAbsent(member.owner, k -> new HashMap<>()).put(rest, runtime);
                m.fieldsBack.computeIfAbsent(ownerRuntime, k -> new HashMap<>()).put(runtime, rest);
                continue;
            }
            String name = rest.substring(0, paren);
            String args = rest.substring(paren + 1, rest.indexOf(')'));
            String named = methodDescriptor(type, args, n -> n);
            String obfDescriptor = methodDescriptor(type, args, obfName);
            String runtime = official.method(ownerObf, obf, obfDescriptor);
            if (runtime.equals(obf) && !name.startsWith("<")) {
                runtime = inherited(ownerRuntime, obf, obfDescriptor, official, runtimeSupers, runtimeToOfficial);
            }
            m.methods.computeIfAbsent(member.owner, k -> new HashMap<>()).put(name + named, runtime);
            m.methodsBack.computeIfAbsent(ownerRuntime, k -> new HashMap<>())
                .put(runtime + methodDescriptor(type, args, runtimeName), name);
        }
        return m;
    }

    /** An override's name: the nearest supertype that declares it, else the official name it has. */
    private static String inherited(String ownerRuntime, String obf, String obfDescriptor, Official official,
                                    Function<String, String[]> runtimeSupers, Map<String, String> runtimeToOfficial) {
        ArrayDeque<String> queue = new ArrayDeque<>();
        Set<String> seen = new HashSet<>();
        String[] first = runtimeSupers.apply(ownerRuntime);
        if (first != null) {
            for (String s : first) if (s != null) queue.add(s);
        }
        while (!queue.isEmpty()) {
            String c = queue.poll();
            if (!seen.add(c)) {
                continue;
            }
            String cObf = runtimeToOfficial.get(c);
            if (cObf == null) {
                continue;
            }
            String runtime = official.method(cObf, obf, obfDescriptor);
            if (!runtime.equals(obf)) {
                return runtime;
            }
            String[] supers = runtimeSupers.apply(c);
            if (supers != null) {
                for (String s : supers) if (s != null) queue.add(s);
            }
        }
        return obf;
    }

    private static String methodDescriptor(String returnType, String args, Function<String, String> names) {
        StringBuilder d = new StringBuilder("(");
        if (!args.isEmpty()) {
            for (String arg : args.split(",")) {
                d.append(descriptor(arg, names));
            }
        }
        return d.append(')').append(descriptor(returnType, names)).toString();
    }

    /** A Java type as ProGuard writes it ("int[]", "net.minecraft.world.phys.Vec3") as a descriptor. */
    static String descriptor(String type, Function<String, String> names) {
        StringBuilder d = new StringBuilder();
        while (type.endsWith("[]")) {
            d.append('[');
            type = type.substring(0, type.length() - 2);
        }
        switch (type) {
            case "void" -> d.append('V');
            case "boolean" -> d.append('Z');
            case "byte" -> d.append('B');
            case "char" -> d.append('C');
            case "short" -> d.append('S');
            case "int" -> d.append('I');
            case "long" -> d.append('J');
            case "float" -> d.append('F');
            case "double" -> d.append('D');
            default -> d.append('L').append(names.apply(type.replace('.', '/'))).append(';');
        }
        return d.toString();
    }
}
