// SPDX-License-Identifier: MIT
package io.conduit;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.stream.Stream;

/**
 * Loads native libraries bundled inside the JAR.
 * <p>
 * Libraries are expected at {@code /native/<os>-<arch>/<libname>} inside the
 * JAR.  On first use the matching library is extracted to a temporary directory
 * and loaded via {@link System#load(String)}.
 * <p>
 * The extraction directory is cleaned up on JVM exit via a shutdown hook.
 * <p>
 * Users can bypass this loader entirely by setting the system properties
 * {@code conduit.jni.path} or {@code conduit.codec.jni.path} to an absolute
 * path, in which case the existing direct-load path in the JNI binding
 * classes takes priority.
 */
final class NativeLoader {

    private static volatile Path extractDir;

    private NativeLoader() {}

    /**
     * Load a native library by name (e.g. "conduit_jni").
     * <p>
     * Resolution order:
     * <ol>
     *   <li>Try {@link System#loadLibrary} (honours {@code java.library.path})</li>
     *   <li>Extract from JAR resource and load</li>
     * </ol>
     *
     * @param libName the platform-independent library name (without prefix/suffix)
     * @throws UnsatisfiedLinkError if the library cannot be found or loaded
     */
    static void load(String libName) {
        // First, try the standard library path — this covers development builds
        // where the .so/.dll sits next to the JAR or in java.library.path.
        try {
            System.loadLibrary(libName);
            return;
        } catch (UnsatisfiedLinkError ignored) {
            // Fall through to JAR extraction
        }

        // Determine the platform-specific resource path inside the JAR
        String osArch = detectOsArch();
        String fileName = mapLibraryFileName(libName);
        String resourcePath = "/native/" + osArch + "/" + fileName;

        InputStream in = NativeLoader.class.getResourceAsStream(resourcePath);
        if (in == null) {
            throw new UnsatisfiedLinkError(
                "Native library not found in JAR: " + resourcePath +
                ". Set java.library.path or provide the library externally.");
        }

        try {
            Path dir = getExtractDir();
            Path libFile = dir.resolve(fileName);

            // Extract the library to the temp directory
            try (OutputStream out = Files.newOutputStream(libFile)) {
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) != -1) {
                    out.write(buf, 0, n);
                }
            } finally {
                in.close();
            }

            System.load(libFile.toAbsolutePath().toString());
        } catch (IOException e) {
            throw new UnsatisfiedLinkError(
                "Failed to extract native library " + resourcePath + ": " + e.getMessage());
        }
    }

    /**
     * Detect the OS and architecture, returning a string like
     * "linux-x86_64", "macos-aarch64", "windows-x86_64".
     */
    static String detectOsArch() {
        String os = System.getProperty("os.name", "").toLowerCase();
        String arch = System.getProperty("os.arch", "").toLowerCase();

        String osName;
        if (os.contains("linux")) {
            osName = "linux";
        } else if (os.contains("mac") || os.contains("darwin")) {
            osName = "macos";
        } else if (os.contains("win")) {
            osName = "windows";
        } else {
            osName = os.replaceAll("\\s+", "_");
        }

        String archName;
        if (arch.equals("amd64") || arch.equals("x86_64")) {
            archName = "x86_64";
        } else if (arch.equals("aarch64") || arch.equals("arm64")) {
            archName = "aarch64";
        } else {
            archName = arch;
        }

        return osName + "-" + archName;
    }

    /**
     * Map a platform-independent library name to the OS-specific file name.
     */
    private static String mapLibraryFileName(String libName) {
        String os = System.getProperty("os.name", "").toLowerCase();
        if (os.contains("win")) {
            return libName + ".dll";
        } else if (os.contains("mac") || os.contains("darwin")) {
            return "lib" + libName + ".dylib";
        } else {
            return "lib" + libName + ".so";
        }
    }

    private static synchronized Path getExtractDir() throws IOException {
        if (extractDir == null) {
            extractDir = Files.createTempDirectory("conduit-native-");
            // Register cleanup on JVM shutdown
            Runtime.getRuntime().addShutdownHook(new Thread(() -> {
                try {
                    // Delete extracted libs and the temp directory
                    if (Files.exists(extractDir)) {
                        try (Stream<Path> entries = Files.list(extractDir)) {
                            entries.forEach(p -> {
                                try { Files.deleteIfExists(p); } catch (IOException ignored) {}
                            });
                        }
                        Files.deleteIfExists(extractDir);
                    }
                } catch (IOException ignored) {}
            }));
        }
        return extractDir;
    }
}
