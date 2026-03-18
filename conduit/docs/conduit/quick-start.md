# Quick Start

[Back to index](index.md)

This tutorial walks through building a complete sender/receiver application using BMDL, bgen, and conduit. The main walkthrough uses C++. Java and Python quick-start examples are at the bottom of this page.

By the end, you will have two programs exchanging typed protocol messages over UDP.

## Step 1: Define the Protocol in BMDL

Create `my-protocol.bmdl.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<bmdl version="2.0">
  <defaults>
    <endian>big</endian>
    <namespace>my_protocol</namespace>
  </defaults>

  <types>
    <type name="uint8" base="uint" bits="8"/>
    <type name="uint16" base="uint" bits="16"/>
    <type name="uint32" base="uint" bits="32"/>
  </types>

  <frame name="MyFrame">
    <field name="msg-type" type="uint8" auto="id"/>
    <field name="length" type="uint16" auto="length"/>
    <payload/>
  </frame>

  <messages>
    <message id="1" name="Heartbeat">
      <field name="sequence" type="uint16"/>
      <field name="status" type="uint8"/>
    </message>

    <message id="2" name="SensorReading">
      <field name="sensor-id" type="uint16"/>
      <field name="value" type="uint32"/>
    </message>
  </messages>
</bmdl>
```

This defines a framed protocol with a `<frame>` that handles message ID dispatch and length management. Each `<message>` has a numeric `id` that matches the frame's `auto="id"` field. The frame automatically wraps and unwraps messages -- you never interact with frame internals. See the [BMDL Language Reference](../bmdl/index.md) for the full language spec.

## Step 2: Generate Code with bgen

```
bgen --input my-protocol.bmdl.xml --output generated/
```

This produces header files in `generated/`, including an umbrella header `my_protocol.hpp` and a session factory function `create_my_frame_session()`. See the [bgen Documentation](../bgen/index.md) for details.

### Naming Convention

BMDL names are converted to C++ identifiers: hyphens become underscores, and the original casing is preserved. For example, `msg-type` becomes `msg_type` (accessor) and `msg_type_` (member). Type names like `SensorReading` stay as-is. See [Naming Conventions](../bgen/naming-conventions.md) for full rules.

## Step 3: Create a Receiver

```cpp
#include "generated/my_protocol.hpp"

#include <conduit/transceiver/transceiver.hpp>
#include <conduit/transceiver/transport/udp.hpp>

#include <iostream>
#include <thread>

int main() {
    using namespace conduit::transceiver;
    using namespace conduit::transceiver::transport;

    // 1. Configure the transceiver with a UDP peer
    UdpConfig udp_cfg;
    udp_cfg.bind_address = "127.0.0.1";
    udp_cfg.bind_port = 5000;

    TransceiverConfig config;
    config.add_peer("rx",
                    my_protocol::create_my_frame_session,
                    std::move(udp_cfg));

    Transceiver receiver(std::move(config));

    // 2. Register typed message handlers
    MessageHandler handler;
    handler
        .on<my_protocol::Heartbeat>([](const my_protocol::Heartbeat& msg) {
            std::cout << "Heartbeat: seq=" << msg.sequence()
                      << " status=" << (int)msg.status() << "\n";
        })
        .on<my_protocol::SensorReading>([](const my_protocol::SensorReading& msg) {
            std::cout << "Sensor " << msg.sensor_id()
                      << ": value=" << msg.value() << "\n";
        });

    receiver.set_handler(std::move(handler));

    // 3. Start and run
    auto result = receiver.start();
    if (!result) {
        std::cerr << "Failed: " << result.error().message() << "\n";
        return 1;
    }

    std::cout << "Receiver listening on port 5000...\n";
    std::this_thread::sleep_for(std::chrono::seconds(60));

    receiver.stop();
    return 0;
}
```

### What Happens

1. [`TransceiverConfig::add_peer()`](configuration.md) registers a peer named `"rx"` with a [UDP transport](transports.md) and a [session factory](sessions-and-codegen.md)
2. [`MessageHandler`](handlers.md) maps each message type to a callback using the fluent `.on<T>()` builder
3. `receiver.start()` returns a [`VoidResult`](error-handling.md) -- check it for errors
4. When a datagram arrives, the session decodes it and the handler fires on the worker thread

## Step 4: Create a Sender

```cpp
#include "generated/my_protocol.hpp"

#include <conduit/transceiver/transceiver.hpp>
#include <conduit/transceiver/transport/udp.hpp>

#include <iostream>
#include <thread>

int main() {
    using namespace conduit::transceiver;
    using namespace conduit::transceiver::transport;

    // 1. Configure sender pointing at the receiver
    UdpConfig udp_cfg;
    udp_cfg.bind_address = "127.0.0.1";
    udp_cfg.remote_address = "127.0.0.1";
    udp_cfg.remote_port = 5000;

    TransceiverConfig config;
    config.add_peer("tx",
                    my_protocol::create_my_frame_session,
                    std::move(udp_cfg));

    Transceiver sender(std::move(config));

    auto result = sender.start();
    if (!result) {
        std::cerr << "Failed: " << result.error().message() << "\n";
        return 1;
    }

    // 2. Send a Heartbeat
    my_protocol::Heartbeat hb;
    hb.set_sequence(1);
    hb.set_status(0);
    auto r1 = sender.send<my_protocol::Heartbeat>(hb);
    if (!r1) std::cerr << "Send failed: " << r1.error().message() << "\n";

    // 3. Send a SensorReading
    my_protocol::SensorReading sr;
    sr.set_sensor_id(42);
    sr.set_value(98765);
    auto r2 = sender.send<my_protocol::SensorReading>(sr);
    if (!r2) std::cerr << "Send failed: " << r2.error().message() << "\n";

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    sender.stop();

    std::cout << "Sent 2 messages.\n";
    return 0;
}
```

### What Happens

1. The sender's UDP config sets `remote_address`/`remote_port` to target the receiver
2. `send<T>(msg)` encodes the message through the session (wrapping it via `MyFrame::wrap()` with the correct `msg-type` and `length`), then transmits the bytes via UDP
3. The sole-peer convenience overload of `send<T>()` is used since there is only one peer

## Data Flow

```
Sender                                          Receiver
──────                                          ────────
Heartbeat hb;
hb.set_sequence(1);

send<Heartbeat>(hb)
  → session.encode_wrap(TYPE_ID, hb)
  → MyFrame::wrap(hb) → {msg_type=1, length=6, payload=hb}
  → BitWriter → [01 00 06 00 01 00]
  → UDP send ──────────────────────────►  UDP recv
                                           → session.decode_frame(bytes)
                                           → Frame → Heartbeat{seq=1, status=0}
                                           → dispatch queue
                                           → worker → handler callback
                                           → "Heartbeat: seq=1 status=0"
```

---

## Java Quick Start

Generate Java code from the same BMDL protocol:

```bash
bgen --input my-protocol.bmdl.xml --output generated/ --language java
```

**Decoding a message:**

```java
import my_protocol.Heartbeat;
import my_protocol.BitReader;

byte[] wireData = ...;  // received from network
BitReader reader = new BitReader(wireData);
Heartbeat msg = Heartbeat.decode(reader);
System.out.println("seq=" + msg.sequence + " status=" + msg.status);
```

**Encoding a message:**

```java
import my_protocol.Heartbeat;
import my_protocol.BitWriter;

Heartbeat hb = new Heartbeat();
hb.sequence = 1;
hb.status = 0;
byte[] wireData = hb.encodeBytes();
// send wireData over network
```

**Using the session for framed messages:**

```java
import my_protocol.MyFrameSession;

MyFrameSession session = new MyFrameSession();

// Decode a frame
List<Map<String, Object>> messages = session.decodeFrame(wireData);
for (Map<String, Object> msg : messages) {
    System.out.println(msg.get("type_name") + ": " + msg.get("payload"));
}

// Encode a framed message
Map<String, Object> result = session.encodeWrap(Heartbeat.TYPE_ID, hb);
byte[] framedBytes = (byte[]) result.get("bytes");
```

For full transport access (TCP, UDP, Serial), use the JNI bindings at `conduit/bindings/java/`.

---

## Python Quick Start

Generate Python code from the same BMDL protocol:

```bash
bgen --input my-protocol.bmdl.xml --output generated/ --language python
```

**Decoding a message:**

```python
from my_protocol.messages import Heartbeat
from my_protocol.bit_io import BitReader

wire_data = b'\x00\x01\x00'  # received from network
reader = BitReader(wire_data)
msg = Heartbeat.decode(reader)
print(f"seq={msg.sequence} status={msg.status}")
```

**Encoding a message:**

```python
from my_protocol.messages import Heartbeat

hb = Heartbeat()
hb.sequence = 1
hb.status = 0
wire_data = hb.encode_bytes()
# send wire_data over network
```

**Using the session for framed messages:**

```python
from my_protocol.sessions import MyFrameSession
from my_protocol.messages import Heartbeat

session = MyFrameSession()

# Decode a frame
messages = session.decode_frame(wire_data)
for msg in messages:
    print(f"{msg['type_name']}: {msg['payload']}")

# Encode a framed message
result = session.encode_wrap(Heartbeat.TYPE_ID, hb)
framed_bytes = result['bytes']
```

For full transport access (TCP, UDP, Serial), use the ctypes bindings at `conduit/bindings/python/`.

---

## Next Steps

- [Error Handling](error-handling.md) -- Understand `Result<T>`, `VoidResult`, and error propagation
- [Transports](transports.md) -- TCP client/server, serial, and transport selection
- [Transceiver](transceiver.md) -- Peer management, lifecycle, statistics
- [Message Handlers](handlers.md) -- Advanced handler patterns (per-peer, groups, catch-all)
- [Configuration](configuration.md) -- Tuning queue size, worker threads, back-pressure
- [Stream Framing](stream-framing.md) -- How TCP/serial streams are reassembled into frames
- [Sessions & Generated Code](sessions-and-codegen.md) -- Direct session usage without the Transceiver
- [Benchmarks & Performance](performance.md) -- Performance characteristics and tuning guidance
- [Limitations & Design Boundaries](limitations.md) -- Scope boundaries and design decisions
