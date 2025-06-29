
# Multithreading

As mentioned in README, corral focuses on single-threaded applications,
due to less overhead and easier reasoning about concurrency. From our own
experience, a single thread driving an event loop is typically sufficient
for most use cases, especially if combined with high-performance asynchronous
I/O solutions like [io_uring](https://en.wikipedia.org/wiki/Io_uring).

However, some applications might require CPU-bound tasks, which would
impact performance or responsiveness if forced to share a thread with I/O.
For example, a BitTorrent client needs to repeatedly hash incoming data to
verify its integrity, and one might want to offload such hashing onto a
separate thread pool.

To address this use case, corral includes a `ThreadPool` class. Its `Run()`
method allows any synchronous function call to be wrapped in an awaitable; when
awaited, the synchronous function runs on another thread, and its result or
exception becomes the result of the awaitable.

A relevant piece of code may look like this (error handling omitted
for clarity):

```cpp
asio::io_service io;
co::ThreadPool tp(io, /*thread_count = */ 8);

struct SHA256 { char data[32] };
SHA256 CalcSHA256(std::span<const char>);

co::Task<void> ReadPacket(tcp::socket& sock) {
    PacketHeader hdr;
    co_await asio::async_read(sock, asio::buffer(&hdr, sizeof(hdr)),
                              co::asio_awaitable);

    auto data = std::make_unique_for_overwrite<char[]>(hdr.DataSize);
    co_await asio::async_read(sock, asio::buffer(data.get(), hdr.DataSize),
                              co::asio_awaitable);

    SHA256 hash = co_await tp.Run([&]{
        return CalcSHA256({data.get(), hdr.DataSize});
    });
    if (memcmp(&hash, &hdr.Hash, sizeof(SHA256))) {
        // handle hash mismatch
    }

    // hash good, process further
}
```

> **Note**: `ThreadPool` is meant for handling CPU-intensive work, rather than
> for doing blocking I/O through libraries, services, or protocols lacking an
> asynchronous API. *Light* use of the thread pool for blocking I/O should work,
> but if you simultaneously submit more `ThreadPool::Run()` tasks than the
> number of worker threads you've started, you run the risk of a deadlock if
> every active thread is waiting for an event that will be performed by a task
> that hasn't yet been scheduled. Even without a deadlock, scalability will be
> poor compared to an async-native implementation.

ThreadPool needs a thread-safe way to notify the main thread of the results
of each task. Since corral is not tied to any specific event loop, a bit of
glue must be provided to teach it how to schedule a callback on the main thread.
This is done by specializing `co::ThreadNotification` for your event loop.
Its member function `void Post(EventLoop&, void (*fn)(void*), void* arg)` must
arrange for `fn(arg)` to be called soon in the main thread. (For convenience,
all arguments will also be passed to the class constructor, with the same values
they will have in `post()`.) One universally applicable approach is opening a
self-pipe and subscribing to events on its read end from the main thread,
but the majority of event loops provide an easier to use option, such as
`asio::post()`, or `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.

Awaitables returned by `ThreadPool::run()` are normally not cancellable,
since it's not possible to interrupt a regular function executing on another
thread in the middle of its execution. This might cause long-running functions
submitted to a thread pool to undesirably delay cancellation of the async tasks
awaiting them. In order to mitigate this issue, a function submitted to a
thread pool may take a final argument of type `co::ThreadPool::CancelToken`,
and query it periodically to check whether it has been requested to wrap up early:

```cpp
SHA256 hash = co_await tp.run(
    [&](co::ThreadPool::CancelToken cancelled) {
        SHA256_CTX ctx;
        const char* begin = data.data();
        size_t size = hdr.DataSize;

        SHA256_Init(&ctx);
        while (size != 0 && !cancelled) {
            size_t chunkSize = std::min(size, 4096);
            SHA256_Update(&ctx, begin, chunkSize);
            begin += chunkSize, size -= chunkSize;
        }

        SHA256 out;
        SHA256_Final(&ctx, out.data);
        return out;
    });
```

In this example, when the calling coroutine gets cancelled, the hasher
will finish its currently processed 4KB-long chunk of data, and then
exit early.

If a function queries its `CancelToken` and it returns true, corral
considers this to be a confirmation of the cancellation. When such
a function returns (normally or through an exception), its result
is discarded, and cancellation continues normally.
