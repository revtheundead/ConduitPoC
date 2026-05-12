# Integrating Conduit into Your Project

How to consume Conduit from a downstream C++, Java, or Python project --
which artifacts to use, how they're packaged in the release tarball, and
how to wire them into your build.

For instructions on *building* Conduit itself, see
[Building & Testing](building.md).  This page is about *using* a built
Conduit installation.

## Library variants at a glance

A full build produces several artifacts, each aimed at a different
consumer.  None of them duplicate each other -- pick the one that fits
your language and deployment shape.

| Artifact | Type | Who uses it |
|----------|------|-------------|
| `libconduit_codec.a` / `conduit_codec.lib` | Static C++ archive | C++ apps that only need encode/decode + framing (no networking).  Has `BitReader`, `BitWriter`, `StreamFramer`, `Logger`, error types. |
| `libconduit.a` / `conduit.lib` | Static C++ archive | C++ apps that want the full `Transceiver` + transports.  PUBLIC-links `libconduit_codec.a`. |
| `libconduit_codec_cabi.so` / `.dylib` / `.dll` | Shared C ABI | Non-C++ codec-only consumers.  Exports `conduit_session_*`, `conduit_decode_frame`, `conduit_encode_*`, `conduit_framer_*`. |
| `libconduit_cabi.so` / `.dylib` / `.dll` | Shared C ABI | Non-C++ consumers that also want the `Transceiver` (peers, transports, handlers, message log, logger control). |
| `libconduit_codec_jni.so` / `.dylib` / `.dll` | JNI shim | Java codec via `JniCodecBinding`. |
| `libconduit_jni.so` / `.dylib` / `.dll` | JNI shim | Java transceiver via `JniNativeBinding`. |
| `conduit-java-<version>.jar` | Java archive | Java consumers (JNI on JDK 11+, Panama FFI on JDK 21+).  Bundles the platform's native libs under `native/<os>-<arch>/` so apps can load them via `NativeLoader` without setting `java.library.path`. |
| `bin/bgen` | Code generator | Reads `.bmdl.xml`, emits C++ / Java / Python sources. |

The static archives expose the full C++ API (`conduit::io::BitReader`,
`conduit::transceiver::Transceiver`, ...).  The C ABI shared libraries
expose **only** the C functions declared in `<conduit/cabi/conduit_cabi.h>`
and `<conduit/cabi/conduit_codec_cabi.h>` -- they are deliberately a
narrow stable surface, not a way to reach the C++ classes.  C++ symbols
inside those `.so` / `.dll` files are hidden.

> **C++ users:** link against the static archives.  Don't try to consume
> the `.so` / `.dll` files from C++ code -- the visibility isn't set up
> for that, and they don't expose `BitReader` / `BitWriter` / `Logger`.

## Release tarball layout

A platform release (`conduit-<version>-linux-x86_64.tar.gz` and
equivalents) is produced by `cmake --install` and looks like:

```
<install-prefix>/
  bin/
    bgen                          # code generator
  include/
    conduit/                      # all public headers
      conduit.hpp                 # umbrella
      cabi/                       # C ABI headers
      core/, io/, traits/, ...
  lib/
    libconduit.a                  # static, full transceiver
    libconduit_codec.a            # static, codec-only
    libconduit_cabi.so            # shared C ABI, full
    libconduit_codec_cabi.so      # shared C ABI, codec-only
    libconduit_jni.so             # JNI shim, full
    libconduit_codec_jni.so       # JNI shim, codec-only
    cmake/conduit/
      conduit-config.cmake        # find_package(conduit) entry
      conduit-config-version.cmake
      conduit-targets.cmake       # imported targets
      conduit-targets-release.cmake
      BgenGenerate.cmake          # bgen_generate(...) helper
```

Extract it anywhere; you don't need to install system-wide.  Pass the
extraction path to consumers via `CMAKE_PREFIX_PATH` (CMake) or
`CONDUIT_CABI_LIB` / `LD_LIBRARY_PATH` (Python / Java).

## C++ consumers (CMake)

The supported integration path is `find_package(conduit)`.  It works
both in-tree (via `add_subdirectory`) and from a release tarball.

### Minimum example

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_app LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(conduit CONFIG REQUIRED)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE conduit::conduit)
```

Build it pointing at the extracted tarball:

```bash
cmake -B build -S . -DCMAKE_PREFIX_PATH=/opt/conduit-1.1.3
cmake --build build
```

> **Use the `conduit::` namespace.**  Bare `target_link_libraries(my_app
> PRIVATE conduit)` does **not** refer to the imported target -- CMake
> falls back to a `-lconduit` link, which finds `libconduit.a` but loses
> the PUBLIC dependency on `libconduit_codec.a`.  Symbols like
> `conduit::io::BitReader::read_bits` (used by bgen-generated code) then
> come up unresolved at link time.

### Imported targets

`find_package(conduit)` defines these (whichever the build produced):

| Target | Backing artifact |
|--------|------------------|
| `conduit::conduit` | `libconduit.a` -- full C++ runtime |
| `conduit::conduit_codec` | `libconduit_codec.a` -- codec-only |
| `conduit::conduit_warnings` | INTERFACE -- internal warning flags (rarely needed by consumers) |
| `conduit::conduit_cabi` | `libconduit_cabi.so` / `.dll` |
| `conduit::conduit_codec_cabi` | `libconduit_codec_cabi.so` / `.dll` |
| `conduit::conduit_jni` | `libconduit_jni.so` / `.dll` |
| `conduit::conduit_codec_jni` | `libconduit_codec_jni.so` / `.dll` |
| `conduit::bgen` | `bin/bgen` (executable) |

Pick the one matching how you want to deploy:

* `conduit::conduit` -- pulls in `conduit_codec` transitively.  Static
  link, single deliverable, full transceiver.
* `conduit::conduit_codec` -- if you only need encode/decode and want a
  smaller binary.
* `conduit::conduit_cabi` -- if you specifically want a shared C-ABI
  surface (e.g. you're shipping a plugin host that loads conduit at
  runtime via `dlopen`).  C++ headers won't be reachable through this
  target.

### Generating sources at build time

The release ships `BgenGenerate.cmake`, which `find_package(conduit)`
makes available:

```cmake
include(${conduit_DIR}/BgenGenerate.cmake)

bgen_generate(
    TARGET     my_protocol_gen
    INPUT      ${CMAKE_CURRENT_SOURCE_DIR}/protocol.bmdl.xml
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen/my_protocol
    NAMESPACE  my_protocol
    LANGUAGE   cpp                # cpp | java | python
)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE conduit::conduit my_protocol_gen)
target_include_directories(my_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen)
```

`my_protocol_gen` is an INTERFACE library that depends on the generated
files; linking it makes the generation run before `my_app` compiles and
adds the right include path.  The helper resolves `conduit::bgen`
automatically -- you don't need to `find_program` for it.

### In-tree (no install) integration

Drop the source tree somewhere and `add_subdirectory` it:

```cmake
add_subdirectory(third_party/conduit)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE conduit)         # in-tree, no namespace
```

In-tree the targets exist with their plain names (`conduit`, `bgen`, ...)
because CMake creates them directly rather than as imported targets.  The
namespaced aliases are only present after `find_package`.

## Java consumers

Add the JAR to your classpath (Maven, Gradle, or plain `-cp`) and let
`NativeLoader` find the bundled native lib for your platform:

```xml
<dependency>
    <groupId>io.conduit</groupId>
    <artifactId>conduit-java</artifactId>
    <version>1.1.3</version>
</dependency>
```

The release JAR carries `native/<os>-<arch>/libconduit_jni.so` (and the
codec / cabi variants) for every platform we publish.  At first call
`NativeLoader` extracts the matching one to a temp directory and loads
it; nothing else is required at runtime.

If you prefer to provide the native library out-of-band (e.g. system
package, custom path), set one of:

* `-Dconduit.jni.path=/path/to/libconduit_jni.so`
* `-Dconduit.cabi.path=/path/to/libconduit_cabi.so` (Panama FFI)
* `-Djava.library.path=/path/containing/the/lib` (then
  `System.loadLibrary` finds it)

The bgen-generated Java sources are self-contained -- they don't depend
on `conduit-java.jar`.  You only need the JAR if you use the
`Transceiver` API.

## Python consumers

Install the Conduit Python package (`pip install` from the
`bindings/python/` source dir) and point it at the matching CABI
library:

```bash
export CONDUIT_CABI_LIB=/opt/conduit-1.1.3/lib/libconduit_cabi.so
```

The bgen-generated Python modules are likewise self-contained;
`CONDUIT_CABI_LIB` is only needed for the transceiver bindings.

## Plain (non-CMake) C++ link line

If you really can't use CMake, the equivalent of `conduit::conduit` on
GNU/Linux is:

```
-I/opt/conduit-1.1.3/include
-L/opt/conduit-1.1.3/lib
-Wl,-Bstatic -lconduit -lconduit_codec -Wl,-Bdynamic
-pthread
```

Order matters -- `conduit` references symbols in `conduit_codec`, so
`conduit_codec` must come *after* it on the link line.  This is exactly
the kind of detail `find_package(conduit)` removes; prefer that path
when it's available.
