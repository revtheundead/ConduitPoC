import java.nio.file.Files;
import java.nio.file.Path;

/**
 * Platform-aware native library resolver for Conduit test files.
 *
 * Checks (in order):
 *   1. Java system property ({@code propertyName})
 *   2. Environment variable ({@code envVar})
 *   3. Auto-detect from project root + build directory, trying both
 *      single-config (Ninja/Make) and multi-config (MSVC) layouts.
 */
public final class TestLibraryResolver {

    private TestLibraryResolver() {}

    /**
     * Resolve the absolute path to a native test library.
     *
     * @param propertyName  system property to check first (e.g. "conduit.codec.test.path")
     * @param envVar        environment variable to check second (e.g. "CONDUIT_CODEC_LIB")
     * @param baseName      base name without prefix/suffix (e.g. "conduit_codec_cabi_test")
     * @return absolute path to the library file
     */
    public static String resolve(String propertyName, String envVar, String baseName) {
        // 1. System property
        String prop = System.getProperty(propertyName, "");
        if (!prop.isEmpty()) return prop;

        // 2. Environment variable
        String env = System.getenv(envVar);
        if (env != null && !env.isEmpty()) return env;

        // 3. Auto-detect from project root
        String projectRoot = System.getProperty("conduit.project.root", "");
        if (projectRoot.isEmpty()) {
            projectRoot = System.getProperty("user.dir", ".");
        }

        String libFileName = platformLibName(baseName);

        // Project-root lib/ directory (primary output location)
        Path libDir = Path.of(projectRoot, "lib", libFileName);
        if (Files.exists(libDir)) return libDir.toString();

        // Legacy: build/lib/<lib>
        Path buildLib = Path.of(projectRoot, "build", "lib", libFileName);
        if (Files.exists(buildLib)) return buildLib.toString();

        // Legacy: build/tests/<lib>
        Path buildTests = Path.of(projectRoot, "build", "tests", libFileName);
        if (Files.exists(buildTests)) return buildTests.toString();

        // Multi-config layout: lib/{Debug,Release,RelWithDebInfo}/<lib>
        for (String config : new String[]{"Debug", "Release", "RelWithDebInfo"}) {
            Path multi = Path.of(projectRoot, "lib", config, libFileName);
            if (Files.exists(multi)) return multi.toString();
        }

        // Fallback (will fail with a clear error at load time)
        return libDir.toString();
    }

    private static String platformLibName(String baseName) {
        String os = System.getProperty("os.name", "").toLowerCase();
        if (os.contains("win")) {
            return baseName + ".dll";
        } else if (os.contains("mac") || os.contains("darwin")) {
            return "lib" + baseName + ".dylib";
        } else {
            return "lib" + baseName + ".so";
        }
    }
}
