# Benchmarks & Performance

[Back to index](index.md)

Conduit delivers sub-microsecond codec latency across all message types and sustains 700K--900K messages/second through TCP loopback end-to-end. The codec alone can process 10--34 million messages/second for decode and 2.8--14.6 million messages/second for encode, depending on message complexity.

> **Note:** All benchmarks below were measured using the C++ backend. Java and Python backends will exhibit higher latencies due to interpreter/JIT overhead, though both now use **byte-aligned fast paths** for whole-byte reads/writes (added in audit Round 4), falling back to bit-by-bit only for unaligned access.

## Codec Latency (Per-Message)

### Encode

| Message | Latency | Wire Size (approx) |
|---------|---------|---------------------|
| Stress: Ping | 69 ns | ~15 B |
| sentry Heartbeat | 91 ns | 14 B |
| CAT001 minimal | 133 ns | ~10 B |
| sentry Telemetry | 139 ns | ~53 B |
| sentry Alert | 142 ns | 47 B |
| sentry Sensor | 146 ns | ~32 B |
| sentry Config | 151 ns | ~48 B |
| CAT253 FormatA | 231 ns | ~25 B |
| Stress: Surveillance typical | 256 ns | ~57 B |
| CAT001 typical | 295 ns | ~20 B |
| Stress: Surveillance maximal | 362 ns | ~100+ B |
| CAT048 typical | 591 ns | 39 B |

### Decode

| Message | Latency | Speedup vs Encode |
|---------|---------|-------------------|
| sentry Heartbeat | 26 ns | 3.5x faster |
| Stress: Ping | 29 ns | 2.4x faster |
| sentry Sensor | 33 ns | 4.4x faster |
| sentry Alert | 45 ns | 3.1x faster |
| sentry Config | 59 ns | 2.6x faster |
| CAT001 minimal | 69 ns | 1.9x faster |
| Stress: Telemetry | 96 ns | 1.5x faster |
| CAT001 typical | 113 ns | 2.6x faster |
| CAT253 FormatA | 119 ns | 1.9x faster |
| CAT253 FormatD | 133 ns | 2.2x faster |
| Stress: Surveillance typical | 163 ns | 1.6x faster |
| CAT048 typical | 267 ns | 2.2x faster |
| Stress: Surveillance maximal | 268 ns | 1.3x faster |

Decode is consistently 1.3--4.4x faster than encode. Encoding involves field validation, bit packing, and length backpatching, while decoding is a straight read-and-assign path.

## Throughput

### Pure Codec (100K batch)

| Operation | Ping | Telemetry | Surveillance (typical) | Surveillance (max) |
|-----------|------|-----------|------------------------|---------------------|
| Encode | 14.6M/s | 7.2M/s | 3.9M/s | 2.8M/s |
| Decode | 34.4M/s | 10.5M/s | 6.1M/s | 3.7M/s |
| Roundtrip | 10.4M/s | 4.4M/s | 2.3M/s | 1.5M/s |

Even the most complex message type (SurveillanceRecord with 14 bitmap items, FX chains, choice, repetitive arrays, scaled fields, strings, and raw bytes) sustains 1.5 million roundtrips/second.

Mixed-workload decode (40% Ping, 35% Telemetry, 25% Surveillance) sustains ~10.9M messages/second.

### TCP End-to-End (100K messages)

| Workload | Msgs/s | Data Rate | Notes |
|----------|--------|-----------|-------|
| Simple (Ping) | 918K | 13.1 MB/s | Smallest message, msg-rate limited |
| Medium (Telemetry) | 803K | 40.6 MB/s | Good balance of count and data |
| Complex (Surveillance) | 738K | 42.2 MB/s | Largest message, bandwidth-limited |
| Mixed (40/35/25) | 2.36M | 94.4 MB/s | Kernel TCP batching benefit |

The codec consumes roughly 7--9% of the end-to-end latency. The remaining 91--93% is TCP socket I/O, kernel buffering, and thread dispatch. **The codec is not the bottleneck** -- the transport layer is.

### UDP Delivery

| Workload | Delivered | Drop Rate |
|----------|-----------|-----------|
| Simple (Ping, ~15 B) | 98,705 / 100K | 1.3% |
| Medium (Telemetry, ~53 B) | 98,657 / 100K | 1.3% |
| Complex (Surveillance, ~57 B) | 62,188 / 100K | 37.8% |
| Mixed | 98,694 / 100K | 1.3% |

Small-to-medium messages achieve 98.7% delivery under aggressive burst conditions (100K fire-and-forget sends). The higher drop rate for complex messages reflects UDP buffer overflow for larger datagrams sent without pacing.

## Back-Pressure

100K messages through a 1024-slot bounded queue with **zero drops** -- the back-pressure mechanism successfully throttles the sender when the receiver's queue fills. Throughput under back-pressure: 888K msgs/s.

## Memory Footprint

### Runtime Components

| Component | Size |
|-----------|------|
| Session object | 8 B (vtable pointer only) |
| BitReader | 32 B |
| BitWriter | 136 B |
| StreamFramer | 48 B |
| HandlerRegistry | 240 B |

### Message Wire vs Memory

| Message Type | In-Memory | Wire Size | Wire/Memory Ratio |
|--------------|-----------|-----------|-------------------|
| HeartbeatBody | 8 B | 14 B | 1.75 (wire > memory) |
| AlertBody | 48 B | 47 B | 0.98 (nearly 1:1) |
| SensorBody | 32 B | ~32 B | ~1.0 |
| Cat048Record | 248 B | 39 B | 0.16 |
| Cat253Record | 296 B | ~30 B | ~0.10 |

The low wire/memory ratio for ASTERIX records (0.10--0.16) reflects the bitmap pattern -- all optional fields are allocated in-memory but only present fields appear on the wire. This is a deliberate trade-off: constant-time field access over memory compactness.

Allocation overhead is negligible: CAT048 encode with allocation = 593 ns vs raw encode = 591 ns.

## Scaling Characteristics

- **Linear with complexity.** Doubling wire size roughly doubles codec time. No hidden O(n^2) paths.
- **Flat across batch size.** Per-message costs remain constant from 10K to 100K iterations -- no memory pressure or cache degradation.
- **Single-threaded dispatch is optimal for lightweight handlers.** Multi-threading adds overhead that only pays off when handler processing exceeds ~1--10 us per message.

## Comparative Context

| Framework | Typical Encode | Typical Decode | Format |
|-----------|---------------|----------------|--------|
| **Conduit (simple)** | 69--91 ns | 26--29 ns | Bit-level binary |
| **Conduit (complex)** | 256--591 ns | 163--267 ns | Bitmap + FX chains |
| Protocol Buffers | 200--500 ns | 100--300 ns | Varint TLV |
| FlatBuffers | 200--400 ns | ~0 ns (zero-copy) | Offset-based |
| Cap'n Proto | 150--300 ns | ~0 ns (zero-copy) | Pointer-based |
| MessagePack | 300--800 ns | 200--500 ns | Compact binary |
| JSON (simdjson) | -- | 300--1000 ns | Text |

Conduit is competitive with or faster than Protocol Buffers for encode, and approaches zero-copy frameworks for decode latency -- while handling bit-level packing, ASTERIX FSPEC bitmaps, FX extension chains, and domain-specific wire formats that byte-level serializers cannot express.

## See Also

- [Configuration](configuration.md) -- Tuning queue size, worker threads, back-pressure
- [Transports](transports.md) -- TCP, UDP, Serial transport options
- [Limitations & Known Issues](limitations.md) -- Current feature gaps across backends
