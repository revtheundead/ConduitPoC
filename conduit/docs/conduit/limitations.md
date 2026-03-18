# Limitations & Design Boundaries

[Back to index](index.md)

This page documents the intentional scope boundaries, architectural limitations, and conscious design trade-offs in Conduit.

## Scope Boundaries

- **No dynamic schema loading.** BMDL schemas must be processed by `bgen` at build time. There is no runtime schema parsing or reflection-based codec generation. This is by design — static code generation enables compile-time type safety, zero-overhead abstractions in C++, and predictable performance across all backends.

- **No runtime protocol negotiation.** Sessions and message types are defined at build time. There is no mechanism for peers to negotiate or discover supported message types at runtime.

- **No built-in encryption or authentication.** Conduit provides raw TCP and UDP transports. TLS, DTLS, or application-layer authentication must be handled externally (e.g. via a TLS-terminating proxy or OS-level tunnel).

- **No built-in message persistence or replay.** Conduit is a live transport library. It does not journal messages to disk or provide replay capabilities. Message logging (to files) is available for diagnostics but is not a durable store.

## Deliberate Design Decisions

### Single sequence counter per session

The session-wide sequence counter is shared across all message types within a session. This is intentional — `encode_wrap` for different message types increments the same counter. Use `reset()` to reset the counter to zero, and `sequence_counter()` to read its current value.

### Worker thread scaling

Adding worker threads only helps when handler processing time exceeds ~1–10 us per message. For lightweight handlers, single-threaded dispatch is faster due to the overhead of cross-thread handoff. See [Benchmarks & Performance](performance.md) for data.

### UDP buffer sizing

The UDP transport explicitly sets `SO_RCVBUF` and `SO_SNDBUF` on the socket to match the configured `recv_buffer_size` and `send_buffer_size` (default 65536 bytes each). This reduces kernel-level drops under burst conditions. Tune via the `send_buffer_size` and `recv_buffer_size` fields in `UdpConfig`.

## Backend Architectural Differences

The C++ backend is the reference implementation. Java and Python backends have near-complete feature parity but differ in idiom:

| Feature | C++ | Java | Python |
|---------|-----|------|--------|
| Field access | Getter/setter methods | Public fields | Public attributes |
| Optional fields | `std::optional<T>` | Boxed types (nullable) | `None` sentinel |
| Variants/choices | `std::variant<...>` | `Object` with `instanceof` | Dynamic typing |
| Error handling | `Result<T>`/`VoidResult` | Exceptions | Exceptions |
| String encoding | Compile-time dispatch | Runtime lookup | Runtime lookup |
| Constraint validation | Setter returns `VoidResult` | Manual validation | Manual validation |
| JSON format | `to_json`/`from_json` (nlohmann) | `toMap()`/`fromMap()` (Map-based) | `to_dict()`/`from_dict()` (dict-based) |

### Performance considerations

Java and Python `BitReader`/`BitWriter` use byte-aligned fast paths for whole-byte reads/writes, falling back to bit-by-bit only for unaligned access. For transport-dominated workloads (where codec time is ~7–9% of total), the codec backend choice has minimal impact.

## See Also

- [Benchmarks & Performance](performance.md) -- Detailed performance data
- [Cross-Language Integration Strategies](cross-language-integration.md) -- Integration approaches
