# Asynchronous IO

OxCo has no IO primitive of its own. File and other OS IO are asio operations
awaited through the asio bridge (`asio.h`): the awaiting task suspends on an
overlapped operation, no thread blocks on the device, and the loop that runs
the `asio::io_context` resumes the task when the operation completes. Every
engine loop that awaits IO already drives an `io_context`: the runtime host
polls the platform's (`Platform::AsyncOps`), and the cooker's
`ImportEventLoop` runs its own.

## Contents

- [Awaiting an operation](#awaiting-an-operation)
- [Threads](#threads)
- [Cancellation](#cancellation)
- [Concurrency](#concurrency)
- [Admission policies](#admission-policies)
- [Decisions](#decisions)

## Awaiting an operation

Any asio `async_*` operation becomes awaitable when given one of the bridge's
completion tokens:

| Token                        | Result of `co_await`                                                  |
| ---------------------------- | --------------------------------------------------------------------- |
| `co::asio_awaitable`         | The operation's values; a non-zero `error_code` throws `system_error` |
| `co::asio_nothrow_awaitable` | The `error_code`, followed by the operation's values, as a tuple      |

A file opened as `asio::random_access_file` uses overlapped IO bound to the
context it was opened with: IOCP on Windows, io_uring on Linux. asio takes its
path as UTF-8.

```cpp
auto ReadHeader(asio::io_context& io, const std::string& utf8_path)
  -> co::Co<std::size_t>
{
  asio::random_access_file file(io, utf8_path, asio::file_base::read_only);
  std::array<std::byte, 64> header {};
  const auto [ec, n] = co_await file.async_read_some_at(
    0, asio::buffer(header), co::asio_nothrow_awaitable);
  if (ec == asio::error::eof) {
    co_return 0; // empty file
  }
  // ...
  co_return n;
}
```

A positional read crossing the end of the file returns the bytes up to the end;
a read at the end transfers nothing and reports `asio::error::eof`. An
operation on a closed file completes with `asio::error::bad_descriptor`; OS
failures arrive as system errors, for example a write to a file opened
read-only fails with `ERROR_ACCESS_DENIED` on Windows and `EBADF` on POSIX.
On Linux, asio's file IO is io_uring: OxCo builds asio with
`ASIO_HAS_IO_URING` and links liburing, so every user of asio agrees on it.

A result that is available at once, such as a read from a memory device, is
returned through `co::Just(value)`, which completes without suspending.

## Threads

IO completes on the loop thread that awaited it, never on a worker. CPU work
that follows IO (decoding, hashing, compression) moves to a `ThreadPool` with
`co_await pool.Run(...)` and comes back to the loop; IO itself is never awaited
through `ThreadPool::Run()` (see [ThreadPool.md](ThreadPool.md)). An IO request
therefore never crosses threads.

## Cancellation

Cancelling a task that awaits an asio operation, because it lost an `AnyOf`,
its nursery was cancelled or a timeout fired, cancels the operation:

- The bridge emits an asio cancellation signal; asio cancels the overlapped
  operation (`CancelIoEx` on Windows).
- The task is released only when the aborted completion arrives, that is after
  the OS has finished with the buffer. A buffer in the task's frame is never
  written after the frame is gone, and the parent (`AnyOf`, the nursery) resumes
  only after that completion ran.
- An aborted operation does not resume the task; the task ends cancelled. If
  the operation completed before the cancellation took effect, the task resumes
  with its result.

`co::IsOperationAborted(ec)` recognizes an aborted operation's error code even
when the `io_context` lives in another DLL: header-only asio gives every module
its own error category instance, so `ec == asio::error::operation_aborted` can
fail across modules.

A wait on an `asio::windows::object_handle` is the exception. asio gives that
wait no per-operation cancellation slot, so cancelling the awaiting task leaves
the wait pending until the event is signalled or the handle's owner calls
`object_handle::cancel()`, which completes the wait with `operation_aborted`.
Code that waits on an event handle owns that `cancel()` call, for example when
it shuts down the component that issued the wait.

## Concurrency

Many operations may be in flight on one handle at once, each with its own
buffer and offset, and each completes independently. A fixed or variable
number of operations is awaited with `co::AllOf`, `co::AnyOf` or `co::MostOf`,
over awaitables or ranges of them; open-ended concurrency uses a `Nursery`.

## Admission policies

OxCo does not bound IO: it has no byte budget or staging quota. A consumer that
limits bytes or operations in flight builds its own admission rule over
`co::Event`, `ParkingLot` or `Semaphore`, which serve waiters in arrival order
and remove a waiter when its task is cancelled.

## Decisions

- **No IO primitive in OxCo.** Awaitable IO is asio's operations through the
  existing bridge; OxCo's primitives stay lean and universal.
- **Admission policies live with their consumers.** Bounding bytes in flight
  needs no OxCo internals; each consumer builds it over `co::Event` and owns its
  rule.
- **IO stays on the loop thread.** Completions run where they were awaited;
  worker threads do CPU work only.
