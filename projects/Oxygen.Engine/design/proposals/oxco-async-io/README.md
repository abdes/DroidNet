# OxCo support for asynchronous IO

Status: proposal

Last updated: 2026-10-31

Serio's [IO and serialization proposal](../serio-io/README.md) makes file IO
an awaitable: `co_await file.AsyncReadAt(offset, buffer)` suspends the calling
coroutine on an overlapped operation and resumes it on completion, without a
thread blocked on the disk. This proposal states what OxCo provides for that:
what it already has, and the tests that prove the chain from a cancelled task
to a cancelled disk operation.

OxCo gains no primitive. Its primitives stay lean and universal; policies
built from them (Content's payload budget, Vortex's staging capacity) live
with their consumers.

## Contents

- [What OxCo already provides](#what-oxco-already-provides)
- [File IO through the asio bridge](#file-io-through-the-asio-bridge)
- [Threads](#threads)
- [Tests](#tests)
- [Docs](#docs)
- [Order](#order)
- [Decisions](#decisions)

## What OxCo already provides

| Need of awaitable IO                                        | OxCo today                                                                                                                                                                                 |
| ----------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Suspend on an OS operation and resume on its completion     | The asio bridge (`asio.h`): any `async_*` operation awaited with `co::asio_awaitable` (throwing) or `co::asio_nothrow_awaitable` (returning the `error_code`)                              |
| Cancel the OS operation when the awaiting task is cancelled | The bridge's `await_cancel` emits an asio cancellation signal; asio cancels the overlapped operation (`CancelIoEx`) and the task resumes with `operation_aborted` once the OS has finished |
| A result available at once (a memory device)                | `co::Ready(value)`: `await_ready()` is true, nothing suspends                                                                                                                              |
| Several reads at once, a fixed or variable number           | `co::AllOf`, `co::AnyOf`, `co::MostOf` over awaitables or ranges of them; `Nursery` for open-ended concurrency                                                                             |
| Waiters served in arrival order, removed when cancelled     | `co::Event`, `ParkingLot` and `Semaphore`, from which a consumer builds its own admission rule                                                                                             |
| Decode on worker threads, back on the loop thread           | `ThreadPool::Run`, with `ThreadNotification<asio::io_context>`                                                                                                                             |
| An event loop that runs completions                         | The runtime host loop polls the platform's `asio::io_context` (`Platform::AsyncOps`); the cooker's `ImportEventLoop` runs one                                                              |

Every loop that would run Serio's awaitable IO already drives an
`asio::io_context`, so a file opened for overlapped IO and bound to that
context completes on the loop's own thread, like any other asio operation.

## File IO through the asio bridge

Serio's `IoQueue` submits a file's operations through its backend. The IOCP
backend uses `asio::random_access_file` (IOCP on Windows, io_uring on Linux):
its operations are the bridge's `async_read_some_at` and
`async_write_some_at`. The IoRing backend waits for its completion event with
`asio::windows::object_handle::async_wait`. OxCo needs no new code for either.
What it needs is proof, since every load and every cook will rely on it:

- A read or write awaited through the bridge completes with the bytes
  transferred and its system error.
- Cancelling the awaiting task, through a `Nursery`, an `AnyOf` or a timeout,
  cancels the operation; the task resumes only after the OS has finished with
  the buffer, so a buffer in the task's frame is never written after the frame
  is gone.
- Many operations on one handle at once complete independently.
- A wait on an event handle resumes on the loop thread when the event is set,
  and cancels cleanly.

## Threads

Nothing changes. IO completes on the loop thread that awaited it; CPU work
(decode, hashing, compression) moves to a `ThreadPool` with
`co_await pool.Run(...)` and comes back. This is the model `Docs/ThreadPool.md`
already describes, and it is the reason no IO request ever crosses threads.

## Tests

`test/Asio_test.cpp` gains file cases, on a temporary file opened for
overlapped IO and an `io_context` run by the test:

| Case                              | Checks                                                                                      |
| --------------------------------- | ------------------------------------------------------------------------------------------- |
| Positional read and write         | Bytes at the requested offsets, counts returned, short read at end of file                  |
| Concurrent operations on a handle | Many reads at different offsets awaited with `AllOf` all return their own bytes             |
| Cancellation by `AnyOf`           | A read losing to a timer resumes with `operation_aborted`, after the OS releases the buffer |
| Cancellation by a nursery         | Cancelling the nursery cancels every in-flight operation and the nursery joins              |
| Errors                            | A read on a closed handle returns the system error through `asio_nothrow_awaitable`         |
| Event handle wait (Windows)       | `object_handle::async_wait` resumes when the event is set, and a cancelled wait resumes     |

## Docs

`Docs/ThreadPool.md` gains a paragraph on IO: awaited on the loop through the
asio bridge, never through `ThreadPool::Run`, whose note already warns against
blocking IO on workers.

## Order

1. The asio file IO and event handle tests, which Serio's `IoQueue` depends
   on, and the `ThreadPool.md` paragraph.

## Decisions

- **No new primitive.** Awaitable IO is asio's operations through the existing
  bridge. Bounding bytes in flight is a consumer's policy over `co::Event`,
  needs no OxCo internals, and lives with that consumer.
- **IO stays on the loop thread.** Completions run where they were awaited;
  worker threads do CPU work only.
