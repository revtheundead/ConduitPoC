# BoundedQueue

[Back to index](index.md)

`BoundedQueue<T>` is a thread-safe, bounded, multi-producer/multi-consumer (MPMC) queue implemented as a ring buffer. It is used internally by the `Transceiver` as the dispatch queue between I/O threads and worker threads, but can also be used standalone.

```cpp
#include <conduit/queue/bounded_queue.hpp>
```

## DropPolicy

```cpp
enum class DropPolicy {
    DropOldest,   // Remove oldest element to make room for new one
    DropNewest,   // Reject new element (oldest preserved)
    Block         // Block until space is available
};
```

## Construction

```cpp
BoundedQueue<InboundMessage> queue(1024, DropPolicy::DropOldest);
BoundedQueue<int> queue(256);  // default: DropOldest
```

`BoundedQueue` is non-copyable and non-movable.

## Producer Operations

| Method | Signature | Description |
|--------|-----------|-------------|
| `try_push` | `bool try_push(T item)` | Non-blocking push. With `Block` policy, behaves like `DropNewest` (increments `dropped` counter and returns `false` when full). |
| `push` | `bool push(T item)` | With `Block` policy, waits until space is available or the queue is closed. With drop policies, identical to `try_push`. |
| `push_for` | `bool push_for(T item, duration timeout)` | Like `push` but with a timeout. With `Block` policy, returns `false` if the timeout expires before space frees up. |

All producers return `false` if the queue is closed. Otherwise the return
value depends on policy:

| Policy | `try_push` (queue full) | `push` (queue full) | `push_for` (queue full) |
|--------|-------------------------|----------------------|--------------------------|
| `DropOldest` | Drops oldest, inserts new, returns `true` | Same | Same |
| `DropNewest` | Increments `dropped`, returns `false` | Same | Same |
| `Block` | Increments `dropped`, returns `false` | Blocks until space or `close()`; returns `true` on insert, `false` on close | Waits up to `timeout`; on insert returns `true`, on timeout returns `false` |

> **Pitfall:** `try_push` with `DropPolicy::Block` is semantically a non-blocking
> request — it never waits — so it can only fail. The `dropped` counter still
> increments to make rejection visible to monitoring; this matches the
> behaviour exercised by `test_bounded_queue.cpp` ("Block try_push increments
> dropped counter").

## Consumer Operations

| Method | Signature | Description |
|--------|-----------|-------------|
| `try_pop` | `optional<T> try_pop()` | Non-blocking pop. Returns `nullopt` if empty. |
| `pop` | `optional<T> pop()` | Blocks until an item is available or queue is closed. Returns `nullopt` if closed and empty. |
| `pop_for` | `optional<T> pop_for(duration timeout)` | Like `pop` but with a timeout. |
| `pop_batch` | `vector<T> pop_batch(size_t max_count)` | Non-blocking batch pop. Returns up to `max_count` items. |

## Queue Management

| Method | Signature | Description |
|--------|-----------|-------------|
| `close()` | `void close()` | Close the queue. Wakes all blocked producers/consumers. |
| `is_closed()` | `bool is_closed()` | Check if the queue is closed. |
| `clear()` | `void clear()` | Remove all items from the queue. |

## Query

| Method | Returns | Description |
|--------|---------|-------------|
| `size()` | `size_t` | Current number of items |
| `capacity()` | `size_t` | Maximum capacity |
| `empty()` | `bool` | True if no items |
| `full()` | `bool` | True if at capacity |
| `stats()` | `const QueueStats&` | Atomic statistics counters |
| `reset_stats()` | `void` | Zero all statistics |

## QueueStats

Atomic counters tracking queue usage:

```cpp
struct QueueStats {
    std::atomic<uint64_t> enqueued{0};    // total items pushed
    std::atomic<uint64_t> dequeued{0};    // total items popped
    std::atomic<uint64_t> dropped{0};     // items dropped (DropOldest/DropNewest)
    std::atomic<size_t> peak_size{0};     // high-water mark
    std::atomic<size_t> current_size{0};  // current fill level

    void reset();  // zero all counters
};
```

### QueueStatsSnapshot

Non-atomic snapshot for consistent reads:

```cpp
struct QueueStatsSnapshot {
    uint64_t enqueued = 0;
    uint64_t dequeued = 0;
    uint64_t dropped = 0;
    size_t peak_size = 0;
    size_t current_size = 0;

    static QueueStatsSnapshot from(const QueueStats& stats);
};
```

```cpp
auto snap = QueueStatsSnapshot::from(queue.stats());
std::cout << "enqueued=" << snap.enqueued
          << " dropped=" << snap.dropped
          << " peak=" << snap.peak_size << "\n";
```

## Relationship to TransceiverConfig

The `Transceiver` creates a `BoundedQueue<InboundMessage>` using settings from `TransceiverConfig::rx_queue`:

```cpp
config.rx_queue.capacity = 4096;
config.rx_queue.drop_policy = queue::DropPolicy::DropOldest;
```

See [Configuration](configuration.md) for details.

## Standalone Usage

```cpp
#include <conduit/queue/bounded_queue.hpp>
#include <thread>

conduit::queue::BoundedQueue<int> q(100, conduit::queue::DropPolicy::Block);

// Producer thread
std::thread producer([&] {
    for (int i = 0; i < 1000; ++i) {
        q.push(i);
    }
    q.close();
});

// Consumer thread
std::thread consumer([&] {
    while (auto item = q.pop()) {
        process(*item);
    }
    // pop() returns nullopt when closed and empty
});

producer.join();
consumer.join();
```

## See Also

- [Configuration](configuration.md) -- `QueueConfig` controls the transceiver's internal dispatch queue
- [Transceiver](transceiver.md) -- Uses `BoundedQueue<InboundMessage>` between I/O and worker threads
