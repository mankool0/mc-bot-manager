package mankool.mcBotClient.plugin;

import net.fabricmc.loader.api.FabricLoader;
import net.fabricmc.loader.api.ModContainer;

import javax.tools.Diagnostic;
import javax.tools.DiagnosticCollector;
import javax.tools.FileObject;
import javax.tools.ForwardingJavaFileManager;
import javax.tools.JavaCompiler;
import javax.tools.JavaFileManager;
import javax.tools.JavaFileObject;
import javax.tools.SimpleJavaFileObject;
import javax.tools.StandardJavaFileManager;
import javax.tools.StandardLocation;
import javax.tools.ToolProvider;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.OutputStream;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/**
 * Compiles a plugin's sources in memory against the classes the running game loads, with the
 * compiler of the runtime the game itself runs on.
 */
final class PluginCompiler {

    record Source(String path, String code) {}

    record Diag(String kind, String path, long line, long column, String message) {}

    /** The classes by binary name, or why there are none. */
    record Result(Map<String, byte[]> classes, List<Diag> diagnostics, String error) {
        boolean ok() {
            return error == null && diagnostics.stream().noneMatch(d -> d.kind().equals("ERROR"));
        }
    }

    private PluginCompiler() {}

    /**
     * Against the running game. Under intermediary names (1.21.x) the sources compile against
     * Mojang's ({@link RuntimeNames}) and the classes are renamed to run.
     */
    static Result compile(List<Source> sources) {
        String namespace = FabricLoader.getInstance().getMappingResolver().getCurrentRuntimeNamespace();
        if (!namespace.equals("intermediary")) {
            // Nothing to rename here, so any copies are from before the instance left 1.21.x.
            RuntimeNames.deleteOtherVersions(null);
            return compile(sources, gameClassPath());
        }
        RuntimeNames names;
        try {
            names = RuntimeNames.get();
        } catch (Exception e) {
            return failed("could not set up Mojang's names for this game: " + e);
        }
        Result named = compile(sources, names.classPath);
        if (!named.ok()) {
            return named;
        }
        try {
            return new Result(Renamer.toRuntime(named.classes(), names.mappings, names.index), named.diagnostics(), null);
        } catch (Exception e) {
            return failed("could not rename the plugin's classes to the game's: " + e);
        }
    }

    static Result compile(List<Source> sources, List<Path> classPath) {
        JavaCompiler javac = ToolProvider.getSystemJavaCompiler();
        if (javac == null) {
            return failed("this Java runtime has no compiler (module jdk.compiler)");
        }
        DiagnosticCollector<JavaFileObject> collected = new DiagnosticCollector<>();
        StandardJavaFileManager standard = javac.getStandardFileManager(collected, Locale.ROOT, StandardCharsets.UTF_8);
        Map<String, ByteArrayOutputStream> output = new LinkedHashMap<>();
        try {
            standard.setLocationFromPaths(StandardLocation.CLASS_PATH, classPath);
            JavaFileManager files = new ForwardingJavaFileManager<>(standard) {
                @Override
                public JavaFileObject getJavaFileForOutput(Location location, String className,
                                                           JavaFileObject.Kind kind, FileObject sibling) {
                    return new SimpleJavaFileObject(URI.create("mem:///" + className.replace('.', '/') + kind.extension), kind) {
                        @Override
                        public OutputStream openOutputStream() {
                            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
                            output.put(className, bytes);
                            return bytes;
                        }
                    };
                }
            };
            List<JavaFileObject> units = new ArrayList<>();
            for (Source source : sources) {
                units.add(new SimpleJavaFileObject(URI.create("string:///" + source.path()), JavaFileObject.Kind.SOURCE) {
                    @Override
                    public CharSequence getCharContent(boolean ignoreEncodingErrors) {
                        return source.code();
                    }
                });
            }
            List<String> options = List.of("-proc:none", "-g", "-parameters", "-encoding", "UTF-8");
            javac.getTask(null, files, collected, options, null, units).call();
        } catch (Exception e) {
            return failed("the compiler failed: " + e);
        }

        List<Diag> diagnostics = new ArrayList<>();
        for (Diagnostic<? extends JavaFileObject> d : collected.getDiagnostics()) {
            String path = d.getSource() == null ? "" : d.getSource().toUri().getPath().replaceFirst("^/", "");
            diagnostics.add(new Diag(d.getKind().name(), path, d.getLineNumber(), d.getColumnNumber(),
                d.getMessage(Locale.ROOT)));
        }
        Map<String, byte[]> classes = new LinkedHashMap<>();
        output.forEach((name, bytes) -> classes.put(name, bytes.toByteArray()));
        return new Result(classes, diagnostics, null);
    }

    private static Result failed(String error) {
        return new Result(Map.of(), List.of(), error);
    }

    /**
     * Everything the game and its mods load from, the game first: each mod's jar (a nested one as
     * the loader extracted it), then the launch class path, which is the libraries. Knot's own
     * list of its class path is only the launch class path.
     */
    static List<Path> gameClassPath() {
        FabricLoader loader = FabricLoader.getInstance();
        List<ModContainer> mods = new ArrayList<>(loader.getAllMods());
        mods.sort(Comparator.comparing(mod -> !mod.getMetadata().getId().equals("minecraft")));
        Set<Path> paths = new LinkedHashSet<>();
        for (ModContainer mod : mods) {
            // The runtime itself, as a mod; its root is java.home.
            if (mod.getMetadata().getId().equals("java")) {
                continue;
            }
            for (Path root : mod.getRootPaths()) {
                paths.add(codeSource(root).toAbsolutePath().normalize());
            }
        }
        for (String entry : System.getProperty("java.class.path", "").split(File.pathSeparator)) {
            if (!entry.isEmpty()) {
                paths.add(Path.of(entry).toAbsolutePath().normalize());
            }
        }
        List<Path> existing = new ArrayList<>();
        for (Path path : paths) {
            if (Files.exists(path)) {
                existing.add(path);
            }
        }
        return existing;
    }

    /** The jar a mod's root is the inside of, or the root itself when it is a directory. */
    static Path codeSource(Path root) {
        if (!root.getFileSystem().provider().getScheme().equals("jar")) {
            return root;
        }
        // jar:<the jar's own URI>!/, with letters like é left unescaped, which Path.of refuses.
        String jar = root.toUri().getRawSchemeSpecificPart();
        return Path.of(URI.create(URI.create(jar.substring(0, jar.lastIndexOf("!/"))).toASCIIString()));
    }
}
