# commbus — task bus for asynchronous adaptor flows

`commbus` is a small, transport-agnostic task executor built for the
corba-adaptor.  It turns "submit a unit of work, wait for it on whatever
threading model my caller uses" into a one-line operation, with full
error/timeout/cancellation propagation and no hidden coupling to TCP,
CORBA, or any other interface.

## Components

| Header                | Purpose                                                    |
|-----------------------|------------------------------------------------------------|
| `commbus/error.hpp`   | Error codes (1000+ range) + `Result<T>` typedef            |
| `commbus/slot.hpp`    | `Slot<T>` — cross-thread one-shot rendezvous primitive     |
| `commbus/context.hpp` | `Context` — per-task handle (make_slot, wait, stop_request)|
| `commbus/wait_registry.hpp` | `WaitRegistry<T>` + `WaitGuard<T>` — correlation-keyed routing for many-to-one delivery |
| `commbus/bus.hpp`     | `CommBus` — bounded queue, worker pool, three submit APIs  |
| `commbus/commbus.hpp` | Umbrella include — pulls in everything above               |

The bus does **not** own a TcpPeer, ORB, or any transport.  Tasks reach
out to whatever interface they need through references captured in their
lambda.  Everything is generic over the interface.

## When to use which submit pattern

| Pattern                                | Caller blocks? | Result delivery       |
|----------------------------------------|----------------|------------------------|
| `submit(task)`                         | No (returns immediately) | error sink only       |
| `submit_with_result<R>(task)`          | Caller `.get()`s the future | future yields `Result<R>` |
| `submit_with_callback<R>(task, on_complete)` | No | callback invoked exactly once with `Result<R>` |

## When to use a Slot

Any time a task body needs to wait for something arriving on a different
thread.  Examples:

- A CORBA reply handler running on an ORB thread.
- A timer callback firing on a scheduler thread.
- A network response that lands in a `TcpPeer::on<T>` handler.
- A signal handler.
- Another bus task wanting to signal its completion to a parent.

The slot is created via `ctx.make_slot<T>()`, handed to whatever will
fulfil it (by value, since it's a `shared_ptr`), and blocked on via
`ctx.wait(slot, timeout)`.

## When to use a WaitRegistry

When several tasks might be waiting concurrently and the responses come
back through a single shared receiver (singleton servant, TcpPeer's
typed handler, etc.).  Each task allocates a correlation id, registers
its slot under that id, and ships the id with its request.  The shared
receiver looks up the slot by id and fulfils it.

If you have only one in-flight task per response type, you don't need a
registry — a bare `Slot<T>` is enough.

## Examples

Working code in `examples/commbus/`:

1. **`01_basics.cpp`** — the three submit patterns.
2. **`02_slot_external.cpp`** — task waits on an external thread; covers
   success, timeout, external failure, and cancellation.
3. **`03_request_response.cpp`** — `WaitRegistry` round-trip via a simulated
   transport with correlation ids.
4. **`04_corba_bridge.cpp`** — blocking and non-blocking CORBA servant
   patterns (no actual TAO link required; CORBA is simulated with
   thread-driven callbacks).
5. **`05_cross_interface.cpp`** — request via one interface, response via
   another, both feeding the same registry.

Build them with the rest of the project; the targets are
`commbus_example_01_basics` … `commbus_example_05_cross_interface`.

## Concurrency rules

- The bus runs tasks on `worker_threads` workers, default 1 (strictly
  serial).  Raise this when you have tasks that park on external waits
  for non-trivial durations.
- `ctx.wait(slot, timeout)` ties up its worker for the wait duration.
  Size `worker_threads` to the maximum number of concurrent external
  waits you expect at peak.
- **Never `.get()` a future returned by `submit_with_result` from inside
  a task body**.  That ties up two workers at best and deadlocks at
  `worker_threads = 1`.  Use `submit_with_callback` for fan-out.
- Cancellation is cooperative.  Tasks that don't poll `stop_requested()`
  and aren't parked in `ctx.wait` keep running until they complete.

## Error model

Everything fails through a single `Result<T> = expected<T, bgen11::Error>`.
The bus's own failure codes live in the 1000+ range (see `error.hpp`)
and coexist with bgen11's codec/encoder codes.  Stray errors with no
future or callback to carry them go to the error sink registered via
`bus.set_error_sink(...)`; default is `std::cerr`.

## Adoption in the adaptor

The `Adaptor` class owns one `commbus::CommBus` (see
`adaptor.hpp`/`adaptor.cpp`).  Servants reach it via `Adaptor::bus()`.
The built-in `bus-ping` command shows the production shape: ORB thread
submits a task with a deadline, task waits on a slot, ORB thread blocks
on the future, replies over CORBA.
