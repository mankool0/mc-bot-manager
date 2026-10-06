package mankool.mcBotClient.plugin;

import net.fabricmc.loader.api.FabricLoader;
import net.fabricmc.loader.api.MappingResolver;

import java.io.BufferedReader;
import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.HexFormat;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.zip.ZipFile;

/**
 * What a plugin compiles against on a game that runs under intermediary names (1.21.x): Mojang's
 * mappings, from the manager, the game and mod jars renamed to them and kept in the game
 * directory, and the libraries as they are. Built on the first load, on the compiler's thread.
 */
final class RuntimeNames {

    private static RuntimeNames instance;

    final Mappings mappings;
    final List<Path> classPath;
    final Map<String, String[]> index;

    private RuntimeNames(Mappings mappings, List<Path> classPath, Map<String, String[]> index) {
        this.mappings = mappings;
        this.classPath = classPath;
        this.index = index;
    }

    static synchronized RuntimeNames get() throws IOException {
        if (instance == null) {
            instance = build();
        }
        return instance;
    }

    private static RuntimeNames build() throws IOException {
        FabricLoader loader = FabricLoader.getInstance();
        String version = loader.getModContainer("minecraft").orElseThrow()
            .getMetadata().getVersion().getFriendlyString();
        Path dir = root().resolve(version);
        Files.createDirectories(dir);

        // The launch class path is the libraries, under their own names. The game, as the game
        // runs it, and every mod are under the game's runtime names.
        Set<Path> libraries = new HashSet<>();
        for (String entry : System.getProperty("java.class.path", "").split(File.pathSeparator)) {
            if (!entry.isEmpty()) libraries.add(Path.of(entry).toAbsolutePath().normalize());
        }
        List<Path> runtime = new ArrayList<>();
        List<Path> plain = new ArrayList<>();
        for (Path p : PluginCompiler.gameClassPath()) {
            if (!Files.isRegularFile(p)) {
                continue;
            }
            if (!libraries.contains(p)) {
                runtime.add(p);
            } else if (!contains(p, "net/minecraft/client/main/Main.class")) {
                // The vanilla jar, under official names, is on the launch class path too.
                plain.add(p);
            }
        }
        Map<String, String[]> runtimeSupers = new HashMap<>();
        for (Path p : runtime) {
            Renamer.index(p, runtimeSupers);
        }

        MappingResolver resolver = loader.getMappingResolver();
        Mappings.Official official = new Mappings.Official() {
            @Override
            public String cls(String internalName) {
                return resolver.mapClassName("official", internalName.replace('/', '.')).replace('.', '/');
            }

            @Override
            public String method(String owner, String name, String descriptor) {
                return resolver.mapMethodName("official", owner.replace('/', '.'), name, descriptor);
            }

            @Override
            public String field(String owner, String name, String descriptor) {
                return resolver.mapFieldName("official", owner.replace('/', '.'), name, descriptor);
            }
        };
        byte[] proguard = PluginHost.get().mojangMappings(version);
        Mappings mappings;
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(
                new ByteArrayInputStream(proguard), StandardCharsets.UTF_8))) {
            mappings = Mappings.read(reader, official, runtimeSupers::get);
        }

        List<Path> named = new ArrayList<>();
        Map<String, String[]> index = new HashMap<>();
        for (Path p : runtime) {
            Path out = dir.resolve("named").resolve(key(p) + ".jar");
            if (Files.exists(out)) {
                Renamer.index(out, index);
            } else {
                Renamer.toNamedJar(p, out, mappings, index);
            }
            named.add(out);
        }
        // Copies of jars the game no longer loads (a mod updated), and a dead writer's leftovers.
        try (var files = Files.list(dir.resolve("named"))) {
            for (Path old : (Iterable<Path>) files::iterator) {
                if (!named.contains(old)) Files.deleteIfExists(old);
            }
        } catch (IOException ignored) {
            // Only disk space.
        }
        deleteOtherVersions(version);
        mappings.classesBack.clear();
        mappings.methodsBack.clear();
        mappings.fieldsBack.clear();
        named.addAll(plain);
        return new RuntimeNames(mappings, named, index);
    }

    private static Path root() {
        return FabricLoader.getInstance().getGameDir().resolve("mcbot").resolve("plugins");
    }

    /**
     * The copies for every game version but {@code keep}, or for all of them when null: an
     * instance moved to another version never builds the old one's again.
     */
    static void deleteOtherVersions(String keep) {
        try (var versions = Files.list(root())) {
            for (Path dir : (Iterable<Path>) versions::iterator) {
                if (!dir.getFileName().toString().equals(keep)) deleteTree(dir);
            }
        } catch (IOException ignored) {
            // Only disk space.
        }
    }

    private static void deleteTree(Path dir) {
        try (var paths = Files.walk(dir)) {
            for (Path p : (Iterable<Path>) paths.sorted(Comparator.reverseOrder())::iterator) {
                Files.deleteIfExists(p);
            }
        } catch (IOException | UncheckedIOException ignored) {
            // A jar another game still has open on Windows; the next build tries again.
        }
    }

    private static boolean contains(Path jar, String entry) {
        try (ZipFile zip = new ZipFile(jar.toFile())) {
            return zip.getEntry(entry) != null;
        } catch (IOException e) {
            return false;
        }
    }

    /** A renamed copy's file name: by content, as the loader extracts nested jars afresh. */
    private static String key(Path jar) throws IOException {
        MessageDigest digest;
        try {
            digest = MessageDigest.getInstance("SHA-1");
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException(e);
        }
        try (InputStream in = Files.newInputStream(jar)) {
            byte[] buffer = new byte[1 << 16];
            for (int n; (n = in.read(buffer)) > 0; ) {
                digest.update(buffer, 0, n);
            }
        }
        return jar.getFileName().toString().replaceAll("\\.jar$", "") + "-"
            + HexFormat.of().formatHex(digest.digest()).substring(0, 12);
    }
}
