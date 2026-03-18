# Conduit Cross-Language Benchmark Results

Benchmark results for the Conduit codec across C++, Python, and Java backends.
All benchmarks use the same BMDL-generated protocols (stress\_protocol and sentry\_link)
with identical message payloads, ensuring wire-compatible, apples-to-apples comparison.

**Environment:**
- OS: Linux 6.18.5 (x86\_64)
- CPU: Cloud VM (single-threaded benchmarks)
- C++: GCC 13.3.0, `-O2` (Release)
- Python: CPython 3.11.14 (ctypes FFI to C ABI)
- Java: OpenJDK 21.0.10 (JNI and Panama FFI to C ABI)

## Per-Message Codec Latency

### Encode (ns/msg)

| Message | C++ | Python | Java (gen) | Slowdown (Py/C++) | Slowdown (Java/C++) |
|---------|----:|-------:|-----------:|------------------:|--------------------:|
| sentry Heartbeat | 67 | 9,593 | 453 | 143x | 6.8x |
| sentry Alert | 123 | 26,359 | 867 | 214x | 7.1x |
| sentry Sensor | 118 | 11,002 | 736 | 93x | 6.2x |
| sentry Config | 134 | 19,320 | 829 | 144x | 6.2x |
| stress Ping | 69* | 9,225 | 474 | 134x | 6.9x |
| stress Telemetry | 139* | 24,632 | 484 | 177x | 3.5x |
| stress Surv typical | 256* | 30,890 | 531 | 121x | 2.1x |
| stress Surv maximal | 358* | 67,968 | 530 | 190x | 1.5x |

\* C++ stress encode measured via stress benchmark (bench\_stress\_codec).

**Key findings:** Python encode is 93-214x slower than C++ due to pure-Python codec
implementation (no FFI overhead — it's pure interpreted code). Java generated codec
encode is 1.5-7x slower than C++, benefiting from JIT compilation.

### Decode (ns/msg)

| Message | C++ | Python (CABI) | Java JNI (CABI) | Java Panama (CABI) | Py/C++ | JNI/C++ | Panama/C++ |
|---------|----:|--------------:|----------------:|-------------------:|-------:|--------:|-----------:|
| sentry Heartbeat | 59 | 5,463 | 930 | 88,600* | 93x | 16x | — |
| sentry Alert | 111 | 7,752 | 1,015 | 58,489* | 70x | 9.1x | — |
| sentry Sensor | 93 | 5,643 | 955 | 56,730* | 61x | 10x | — |
| sentry Config | 135 | 6,923 | 1,315 | 57,272* | 51x | 9.7x | — |
| stress Ping | 29 | 5,211 | 911 | 45,352* | 180x | 31x | — |
| stress Telemetry | 95 | 8,083 | 1,122 | 40,748* | 85x | 12x | — |
| stress Surv typical | 163 | 9,002 | 1,363 | 49,244* | 55x | 8.4x | — |
| stress Surv maximal | 268 | 13,994 | 1,619 | 33,241* | 52x | 6.0x | — |

\* Panama per-message numbers are from cold-start measurements. After JIT warmup,
Panama achieves similar throughput to JNI (see Stress section below).

**Key findings:** Python CABI decode (ctypes FFI) is 51-180x slower than C++ due to
ctypes marshaling overhead. Java JNI decode is 6-31x slower, with the overhead
dominated by JNI boundary crossing (byte array copies, JNI env lookups).

## 100K Stress Test (msgs/s)

### Encode Throughput

| Message | C++ | Python | Java (gen) |
|---------|----:|-------:|-----------:|
| Ping | 14,600,000 | 104,548 | 3,023,840 |
| TelemetryReport | 7,200,000 | 41,686 | 3,102,843 |
| Surv typical | 3,900,000 | 31,371 | 2,746,662 |
| Surv maximal | 2,800,000 | 15,577 | 2,850,561 |

### Decode Throughput

| Message | C++ | Python (CABI) | Java JNI | Java Panama |
|---------|----:|--------------:|---------:|------------:|
| Ping | 34,400,000 | 197,184 | 1,458,086 | 1,488,070 |
| TelemetryReport | 10,500,000 | 122,414 | 1,256,394 | 1,172,018 |
| Surv typical | 6,100,000 | 111,353 | 975,091 | 908,068 |
| Surv maximal | 3,700,000 | 72,128 | 763,106 | 708,608 |

### Roundtrip Throughput

| Message | C++ | Python | Java JNI | Java Panama |
|---------|----:|-------:|---------:|------------:|
| Ping | 10,400,000 | 60,773 | 3,036,105 | 1,913,516 |
| TelemetryReport | 4,400,000 | 28,373 | 3,104,541 | 1,892,696 |
| Surv typical | 2,300,000 | 21,762 | 2,890,992 | 1,751,139 |
| Surv maximal | 1,500,000 | 11,742 | 2,984,443 | 1,785,829 |

### Mixed Workload (40% Ping / 35% Telemetry / 25% Surveillance)

| Backend | Decode msgs/s |
|---------|------------:|
| C++ | ~10,000,000 |
| Python (CABI) | 137,804 |
| Java JNI | 1,267,597 |
| Java Panama | 1,180,433 |

## 10K Batch Throughput

### Encode (msgs/s)

| Message | Python | Java (gen) |
|---------|-------:|-----------:|
| sentry Heartbeat | 109,538 | 3,097,518 |
| sentry Alert | 36,277 | 1,630,167 |
| stress Ping | 106,080 | 3,030,381 |
| stress Telemetry | 42,027 | 3,173,399 |

### Decode (msgs/s)

| Message | Python (CABI) | Java JNI | Java Panama |
|---------|------------:|---------:|------------:|
| sentry Heartbeat | 190,944 | 1,641,763 | 1,431,786 |
| sentry Alert | 132,849 | 1,450,552 | 1,315,330 |
| stress Ping | 197,605 | 1,644,149 | 1,527,138 |
| stress Telemetry | 122,227 | 1,266,740 | 1,179,591 |

## JNI vs Panama FFI Comparison

At sustained throughput (100K messages), JNI and Panama achieve comparable performance:

| Metric | JNI | Panama | Winner |
|--------|----:|-------:|--------|
| Ping decode (msgs/s) | 1,458,086 | 1,488,070 | Panama (+2%) |
| Telemetry decode | 1,256,394 | 1,172,018 | JNI (+7%) |
| Surv typical decode | 975,091 | 908,068 | JNI (+7%) |
| Surv maximal decode | 763,106 | 708,608 | JNI (+8%) |
| Mixed decode | 1,267,597 | 1,180,433 | JNI (+7%) |

**Conclusion:** JNI holds a slight edge (~7%) for complex messages due to lower per-call
overhead for byte array transfers. Panama is competitive and avoids the need for a
separate JNI shared library, making it simpler to deploy. Both achieve >700K msgs/s
even for the most complex message type.

## Architecture Notes

- **C++ benchmarks** use bgen-generated C++ code directly (Catch2 BENCHMARK macros)
- **Python benchmarks** use bgen-generated Python code for encode; C ABI (ctypes) for decode
- **Java JNI benchmarks** use bgen-generated Java code for encode; C ABI (JNI bridge) for decode
- **Java Panama benchmarks** use bgen-generated Java code for encode; C ABI (Panama FFI) for decode
- All languages produce **wire-compatible** output from the same BMDL schema
- Python/Java decode goes through the C ABI shared library (`conduit_codec_cabi_bench`)
  which bundles the C++ codec with stress\_frame and sentry\_link sessions registered
