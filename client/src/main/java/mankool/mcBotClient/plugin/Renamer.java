package mankool.mcBotClient.plugin;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.Type;
import org.objectweb.asm.commons.ClassRemapper;
import org.objectweb.asm.commons.Remapper;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayDeque;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;
import java.util.function.Function;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipOutputStream;

/**
 * Class files from the names the game runs under to Mojang's and back (see {@link Mappings}).
 * Supertypes come from an index of the renamed jars, Mojang-named, and a plugin's own classes.
 */
final class Renamer {

    private Renamer() {}

    /**
     * {@code in}, renamed to Mojang's names, written to {@code out} with declarations only - what
     * a compiler reads, and quick to make. Each class's supertypes go into {@code index}.
     */
    static void toNamedJar(Path in, Path out, Mappings m, Map<String, String[]> index) throws IOException {
        Remapper names = new Remapper() {
            @Override
            public String map(String internalName) {
                return m.classesBack.getOrDefault(internalName, internalName);
            }

            @Override
            public String mapMethodName(String owner, String name, String descriptor) {
                Map<String, String> declared = m.methodsBack.get(owner);
                String named = declared == null ? null : declared.get(name + descriptor);
                return named == null ? name : named;
            }

            @Override
            public String mapFieldName(String owner, String name, String descriptor) {
                Map<String, String> declared = m.fieldsBack.get(owner);
                String named = declared == null ? null : declared.get(name);
                return named == null ? name : named;
            }

            @Override
            public String mapRecordComponentName(String owner, String name, String descriptor) {
                return mapFieldName(owner, name, descriptor);
            }
        };
        Files.createDirectories(out.getParent());
        Path part = Files.createTempFile(out.getParent(), out.getFileName().toString(), ".part");
        try (ZipFile zip = new ZipFile(in.toFile());
             ZipOutputStream jar = new ZipOutputStream(Files.newOutputStream(part))) {
            for (Enumeration<? extends ZipEntry> e = zip.entries(); e.hasMoreElements(); ) {
                ZipEntry entry = e.nextElement();
                if (!isClass(entry)) {
                    continue;
                }
                byte[] bytes;
                try (InputStream s = zip.getInputStream(entry)) {
                    bytes = s.readAllBytes();
                }
                ClassReader reader = new ClassReader(bytes);
                ClassWriter writer = new ClassWriter(0);
                reader.accept(new ClassRemapper(writer, names), ClassReader.SKIP_CODE | ClassReader.SKIP_DEBUG | ClassReader.SKIP_FRAMES);
                byte[] renamed = writer.toByteArray();
                String name = index(renamed, index);
                jar.putNextEntry(new ZipEntry(name + ".class"));
                jar.write(renamed);
                jar.closeEntry();
            }
        }
        Files.move(part, out, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
    }

    /** Every class in {@code jar}, by the names it has there, with its supertypes, into {@code index}. */
    static void index(Path jar, Map<String, String[]> index) throws IOException {
        try (ZipFile zip = new ZipFile(jar.toFile())) {
            for (Enumeration<? extends ZipEntry> e = zip.entries(); e.hasMoreElements(); ) {
                ZipEntry entry = e.nextElement();
                if (isClass(entry)) {
                    try (InputStream s = zip.getInputStream(entry)) {
                        index(s.readAllBytes(), index);
                    }
                }
            }
        }
    }

    /**
     * A plugin's classes, compiled against Mojang's names, under the names the game runs under.
     * A member is looked for up its owner's supertypes: javac names the class a call is made
     * through, not the one that declares it.
     */
    static Map<String, byte[]> toRuntime(Map<String, byte[]> classes, Mappings m, Map<String, String[]> gameIndex) {
        Map<String, String[]> own = new HashMap<>();
        for (byte[] bytes : classes.values()) {
            index(bytes, own);
        }
        Remapper names = new Remapper() {
            @Override
            public String map(String internalName) {
                return m.classes.getOrDefault(internalName, internalName);
            }

            @Override
            public String mapMethodName(String owner, String name, String descriptor) {
                if (name.startsWith("<")) {
                    return name;
                }
                String found = find(owner, c -> {
                    Map<String, String> declared = m.methods.get(c);
                    return declared == null ? null : declared.get(name + descriptor);
                });
                return found == null ? name : found;
            }

            @Override
            public String mapFieldName(String owner, String name, String descriptor) {
                String found = find(owner, c -> {
                    Map<String, String> declared = m.fields.get(c);
                    return declared == null ? null : declared.get(name);
                });
                return found == null ? name : found;
            }

            @Override
            public String mapRecordComponentName(String owner, String name, String descriptor) {
                return mapFieldName(owner, name, descriptor);
            }

            // A lambda's method is named for the interface it implements, which the call site's
            // descriptor returns; that interface has one abstract method by that name.
            @Override
            public String mapInvokeDynamicMethodName(String name, String descriptor) {
                Type made = Type.getReturnType(descriptor);
                if (made.getSort() != Type.OBJECT) {
                    return name;
                }
                String found = find(made.getInternalName(), c -> {
                    Map<String, String> declared = m.methods.get(c);
                    if (declared == null) {
                        return null;
                    }
                    Set<String> runtime = new HashSet<>();
                    declared.forEach((key, value) -> {
                        if (key.startsWith(name + "(")) runtime.add(value);
                    });
                    return runtime.size() == 1 ? runtime.iterator().next() : null;
                });
                return found == null ? name : found;
            }

            private String find(String owner, Function<String, String> lookup) {
                ArrayDeque<String> queue = new ArrayDeque<>();
                Set<String> seen = new HashSet<>();
                queue.add(owner);
                while (!queue.isEmpty()) {
                    String c = queue.poll();
                    if (!seen.add(c)) {
                        continue;
                    }
                    String hit = lookup.apply(c);
                    if (hit != null) {
                        return hit;
                    }
                    String[] supers = own.containsKey(c) ? own.get(c) : gameIndex.get(c);
                    if (supers != null) {
                        for (String s : supers) {
                            if (s != null) queue.add(s);
                        }
                    }
                }
                return null;
            }
        };
        Map<String, byte[]> out = new LinkedHashMap<>();
        classes.forEach((name, bytes) -> {
            ClassWriter writer = new ClassWriter(0);
            new ClassReader(bytes).accept(new ClassRemapper(writer, names), 0);
            out.put(name, writer.toByteArray());
        });
        return out;
    }

    private static boolean isClass(ZipEntry entry) {
        String n = entry.getName();
        return n.endsWith(".class") && !n.startsWith("META-INF/") && !n.endsWith("module-info.class");
    }

    /** The class's name, after putting its supertypes into {@code index}. */
    private static String index(byte[] bytes, Map<String, String[]> index) {
        ClassReader reader = new ClassReader(bytes);
        String[] interfaces = reader.getInterfaces();
        String[] supers = new String[interfaces.length + 1];
        supers[0] = reader.getSuperName();
        System.arraycopy(interfaces, 0, supers, 1, interfaces.length);
        index.put(reader.getClassName(), supers);
        return reader.getClassName();
    }
}
