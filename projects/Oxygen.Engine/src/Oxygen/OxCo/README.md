# Corral — lightweight structured concurrency for C++20

Oxygen::Co library uses substantial code from the [corral
library](https://github.com/hudson-trading/corral). Credits go to its authors
for the original implementation.

Several modifications were made to adapt the library to the own usage
requirements of Oxygen and add missing features that do not fit into the
original library objectives.

## Purpose

Corral is a C++ concurrency library that implements cooperative
single-threaded multitasking using C++20 coroutines. Its design is
based on our experience using coroutines to support asynchronous I/O in
real-world production code. Users familiar with the
[Trio](https://github.com/python-trio/trio) library for Python will
find a lot here that looks familiar. A few of corral's design goals are:

* ***[Structured concurrency](https://vorpus.org/blog/notes-on-structured-concurrency-or-go-statement-considered-harmful/)***
  baked in: tasks are organized into a tree of parent-child
  relationships, where the parent is responsible for waiting for its
  children to finish and propagates any exceptions that the children
  raise.  This allows certain crucial features like resource
  management, task cancellation, and error handling to Just Work™ the
  way people would expect them to.

* ***I/O and event loop agnostic***: like quite a few other
  companies with decades of history, we have our own homegrown
  implementations of asynchronous I/O and event loops. We wanted
  to be able to use coroutines with them, as well as pretty much
  any other existing solution for asynchronous I/O (such as Asio,
  libuv, or libevent).

* ***Bridging with callbacks***: the majority of existing code uses
  callbacks for asynchronous I/O; rewriting all of it from the ground
  up, while entertaining, tends not to be a realistic option.  We
  needed a way to have coroutine "pockets" in the middle of legacy
  code, being able call or be called from older code that's still
  using callbacks — so people could onboard gradually, one small piece
  at a time, getting benefit from these small pieces immediately.

Corral focuses on **single-threaded** applications because this results in
a simpler design, less overhead, and easier reasoning about
concurrency hazards. (In a single-threaded environment with
cooperative multitasking, you know that you have exclusive access to
all state in between `co_await` points.)  Multiple threads can each
run their own "corral universe", as long as tasks that belong to
different threads do not interact with each other.

## Motivating example

The code snippet below establishes a TCP connection to one of two
remote servers, whichever responds first, and returns the socket.

```cpp
using tcp = boost::asio::tcp;
boost::asio::io_service io_service;

co::Task<tcp::socket> MyConnect(tcp::endpoint main, tcp::endpoint backup) {
    tcp::socket mainSock(io_service), backupSock(io_service);

    auto [mainErr, backupErr, timeout] = co_await co::AnyOf(
        // Main connection attempt
        mainSock.async_connect(main, co::asio_nothrow_awaitable),

        // Backup connection, with staggered startup
        [&]() -> co::Task<boost::system::error_code> {
            co_await co::SleepFor(io_service, 100ms);
            co_return co_await backupSock.async_connect(
                backup, co::asio_nothrow_awaitable);
        },

        // Timeout on the whole thing
        co::sleepFor(io_service, 3s));

    if (mainErr && !*mainErr) {
        co_return mainSock;
    } else if (backupErr && !*backupErr) {
        co_return backupSock;
    } else {
        throw std::runtime_error("both connections failed");
    }
}
```

## LICENSE

Oxygen::Co library uses substantial code from the [corral
library](https://github.com/hudson-trading/corral). Credits go to its authors
for the original implementation.

---
MIT License

Copyright (c) 2024 Hudson River Trading LLC <opensource@hudson-trading.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
