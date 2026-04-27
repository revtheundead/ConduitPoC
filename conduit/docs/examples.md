# Examples

The `examples/` directory contains complete working applications that demonstrate how to use Conduit across C++, Java 11, Java 21, and Python. All four implementations exchange the same ASTERIX surveillance protocol messages over TCP, proving cross-language wire compatibility.

## xcvr-cpp -- C++ ASTERIX Transceiver

**What it does.** Two executables -- `poc_app` (TCP client) and `dummy_peer` (TCP server/client) -- that exchange ASTERIX Cat007, Cat021, Cat048, and Cat253 messages using Conduit's transceiver framework.

**How it works.** Each executable creates a `Transceiver` with a TCP transport, registers typed message handlers (callbacks for specific message types), and sends randomly-generated ASTERIX messages at a configurable interval. `dummy_peer` can run as either a server (using `asterix-alt` session with reversed directions) or a client (using `asterix` session). Both apps log messages, track statistics, and handle graceful Ctrl+C shutdown.

**Practices demonstrated:**
- Typed message handlers with compile-time type safety
- TCP transport configuration (client and server modes)
- Cross-namespace interoperability (`asterix` vs `asterix-alt` with swapped directions)
- Pre-generated protocol code from BMDL definitions
- Message logging with `SEPARATE_DIRECTION` mode
- Connection state change and error callbacks

**How to build:**

```bash
# As part of the conduit build:
cmake -B build -S conduit -DCONDUIT_BUILD_EXAMPLES=ON -DCONDUIT_BUILD_BGEN=ON
cmake --build build

# Binaries are placed in conduit/examples/xcvr-cpp/
```

**How to run:**

```bash
# Start server
./conduit/examples/xcvr-cpp/dummy_peer server --port 5000 --interval-ms 500

# In another terminal, start client
./conduit/examples/xcvr-cpp/poc_app 127.0.0.1 5000 --interval-ms 500
```

**CLI options:**

- `dummy_peer server [--port N] [--interval-ms N] [--log-dir DIR] [--log-prefix PREFIX] [--log-filename PATTERN]`
- `dummy_peer client [host] [port] [--interval-ms N] [--log-dir DIR] [--log-prefix PREFIX] [--log-filename PATTERN]`
- `poc_app [host] [port] [--interval-ms N] [--log-dir DIR] [--log-prefix PREFIX] [--log-filename PATTERN]`

`--log-filename` accepts a pattern with `{peer}` and `{direction}` placeholders (see [Message Logging](conduit/transceiver.md#custom-filename-pattern)). The `--log-*` flags configure the message log; if omitted, message logging is disabled.

---

## xcvr-java11 -- Java 11 / JNI

**What it does.** The same ASTERIX transceiver implemented in Java 11 using JNI bindings to the native `conduit_jni` shared library. Provides `PocApp` (client) and `DummyPeer` (server/client) with identical functionality to the C++ version.

**How it works.** Java code calls into the Conduit runtime through JNI. The `conduit-java` library provides `Transceiver`, `TransportConfig`, and `ConduitNative` classes that bridge to the C++ implementation. Generated Java classes from BMDL provide type-safe message construction and parsing. Code generation can run automatically during Maven's `generate-sources` phase or be skipped with `-Dskip.bgen=true`.

**Practices demonstrated:**
- JNI native binding pattern (`JniNativeBinding`, `JniCodecBinding`)
- Maven integration with native build artifacts (`conduit.build.dir` property)
- bgen code generation integrated into Maven build lifecycle
- Cross-language protocol interoperability (Java client talks to C++ server)

**Prerequisites:** conduit-java JAR installed to local Maven repo, `conduit_jni` shared library built. Requires `javac`/`java` (JDK 11+) and either Maven 3.6+ or Gradle 7+.

**How to build (Maven):**

```bash
# 1. Build conduit with CABI + JNI + bgen
cmake -B build -S conduit -DCONDUIT_BUILD_CABI=ON -DCONDUIT_BUILD_JNI=ON -DCONDUIT_BUILD_BGEN=ON
cmake --build build

# 2. Install conduit-java to local Maven repo
mvn install -q -f conduit/bindings/java/pom.xml

# 3. Build the example
mvn package -q -f conduit/examples/xcvr-java11/pom.xml -Dskip.bgen=true \
    -Dconduit.build.dir=$(pwd)/build
```

**How to build (Gradle):**

```bash
# After steps 1-2 above:
cd conduit/examples/xcvr-java11
gradle build
```

**How to run:**

The native shared libraries in `conduit/lib/` must be on the JVM library path. The Maven and Gradle run targets set `-Djava.library.path` automatically. If running the JAR directly, set `LD_LIBRARY_PATH` (Linux), `DYLD_LIBRARY_PATH` (macOS), or `PATH` (Windows) to include `conduit/lib/`, or pass `-Djava.library.path` to the JVM. See [Native library path](building.md#native-library-path-conduitlib) for details.

```bash
# Maven
mvn exec:java -f conduit/examples/xcvr-java11/pom.xml -PrunDummyPeer \
    -Dconduit.build.dir=$(pwd)/build \
    -Dexec.appArgs="server --port 5000 --interval-ms 500"

# Gradle
cd conduit/examples/xcvr-java11
gradle runDummyPeer -DappArgs="server --port 5000 --interval-ms 500"
```

---

## xcvr-java21 -- Java 21 / Panama FFI

**What it does.** The same ASTERIX transceiver using Java 21's Panama Foreign Function & Memory API instead of JNI. This provides a pure-Java foreign function interface without requiring a JNI shared library -- only the `conduit_cabi` shared library is needed.

**How it works.** Java code uses `PanamaNativeBinding` and `PanamaCodecBinding` to call CABI functions directly via `java.lang.foreign.Linker`. The `--enable-preview` and `--enable-native-access=ALL-UNNAMED` JVM flags are required.

**Practices demonstrated:**
- Panama FFI binding pattern (no JNI compilation step)
- `--enable-preview` usage for Foreign Function & Memory API
- Direct CABI function invocation from Java

**Prerequisites:** conduit-java JAR installed, `conduit_cabi` shared library built. Requires `javac`/`java` (JDK 21+) and either Maven 3.6+ or Gradle 7+.

**How to build (Maven):**

```bash
# 1. Build conduit with CABI + bgen
cmake -B build -S conduit -DCONDUIT_BUILD_CABI=ON -DCONDUIT_BUILD_BGEN=ON
cmake --build build

# 2. Install conduit-java to local Maven repo
mvn install -q -f conduit/bindings/java/pom.xml

# 3. Build the example
mvn package -q -f conduit/examples/xcvr-java21/pom.xml -Dskip.bgen=true
```

**How to build (Gradle):**

```bash
# After steps 1-2 above:
cd conduit/examples/xcvr-java21
gradle build
```

**How to run:**

The CABI shared library in `conduit/lib/` must be on the JVM library path. The Maven and Gradle run targets set `-Djava.library.path` and `-Dconduit.cabi.path` automatically. If running the JAR directly, see [Native library path](building.md#native-library-path-conduitlib) for details.

```bash
# Maven
MAVEN_OPTS="--enable-preview --enable-native-access=ALL-UNNAMED" \
mvn exec:java -f conduit/examples/xcvr-java21/pom.xml -PrunDummyPeer \
    -Dexec.appArgs="server --port 5000 --interval-ms 500"

# Gradle
cd conduit/examples/xcvr-java21
gradle runDummyPeer -DappArgs="server --port 5000 --interval-ms 500"
```

---

## xcvr-python -- Python / ctypes

**What it does.** The same ASTERIX transceiver in Python using ctypes bindings to the `conduit_cabi` shared library.

**How it works.** The `conduit` Python package wraps CABI functions using `ctypes.CDLL`. Generated Python modules from BMDL provide dataclass-like message types with `encode_bytes()` / `decode()` methods. The `CONDUIT_CABI_LIB` environment variable tells the bindings where to find the native library.

**Practices demonstrated:**
- ctypes-based native binding pattern
- Python package structure with `pyproject.toml`
- Environment-based library path configuration (`CONDUIT_CABI_LIB`)
- Entry points for CLI commands (`poc-app`, `dummy-peer`)

**Prerequisites:** conduit Python bindings installed, `conduit_cabi` shared library built.

**How to build:**

```bash
# 1. Build conduit with CABI + bgen
cmake -B build -S conduit -DCONDUIT_BUILD_CABI=ON -DCONDUIT_BUILD_BGEN=ON
cmake --build build

# 2. Install bindings and example
pip install conduit/bindings/python/
pip install conduit/examples/xcvr-python/
```

**How to run:**

The Python bindings need `CONDUIT_CABI_LIB` set to the full path of the CABI shared library in `conduit/lib/`. You may also need `LD_LIBRARY_PATH` (Linux) or `DYLD_LIBRARY_PATH` (macOS) if the CABI library has dependencies in the same directory. See [Native library path](building.md#native-library-path-conduitlib) for all platforms.

```bash
# Run from the repository root directory
export CONDUIT_CABI_LIB=$(pwd)/conduit/lib/libconduit_cabi.so

# Server (using pip-installed entry points)
dummy-peer server --port 5000 --interval-ms 500

# Client (in another terminal)
poc-app 127.0.0.1 5000 --interval-ms 500
```

Alternatively, run directly from the source tree without pip-installing the example. The `src.<module>` import path requires Python's working directory to be `conduit/examples/xcvr-python/` (or `PYTHONPATH` set to it):

```bash
export CONDUIT_CABI_LIB=$(pwd)/conduit/lib/libconduit_cabi.so

cd conduit/examples/xcvr-python
python -m src.dummy_peer server --port 5000 --interval-ms 500
python -m src.poc_app 127.0.0.1 5000 --interval-ms 500
```

---

## Building & Testing

For build instructions (compiler selection, CMake options, flags) and test runner usage, see [Building & Testing](building.md).
