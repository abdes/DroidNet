//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <asio/buffer.hpp>
#include <asio/error.hpp>
#include <asio/file_base.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/address_v4.hpp>
#include <asio/steady_timer.hpp>
#include <asio/system_error.hpp>

#include <Oxygen/Base/Logging.h>
#define ASIO_NO_TYPEID
#include <random>

#include <asio/ip/address.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/random_access_file.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>
#include <asio/write_at.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Platforms.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Semaphore.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/OxCo/asio.h>
#include <Oxygen/Testing/GTest.h>

#if defined(OXYGEN_WINDOWS)
#  include <windows.h> // IWYU pragma: keep

#  include <asio/windows/object_handle.hpp>
#endif

using oxygen::ScopeGuard;
using oxygen::co::AllOf;
using oxygen::co::AnyOf;
using oxygen::co::asio_awaitable;
using oxygen::co::asio_nothrow_awaitable;
using oxygen::co::Co;
using oxygen::co::IsOperationAborted;
using oxygen::co::kCancel;
using oxygen::co::kJoin;
using oxygen::co::Run;
using oxygen::co::Semaphore;
using oxygen::co::SleepFor;
using oxygen::co::ThreadPool;

namespace {

using Clock = std::chrono::high_resolution_clock;
using namespace std::chrono_literals;

class AsioTestFixture : public testing::Test {
protected:
  std::unique_ptr<asio::io_context> io_;

  auto SetUp() -> void override
  {
    testing::internal::CaptureStderr();
    io_ = std::make_unique<asio::io_context>();
  }

  auto TearDown() -> void override
  {
    io_.reset();
    const auto captured_stderr = testing::internal::GetCapturedStderr();
    std::cout << "Captured stderr:\n" << captured_stderr << '\n';
  }
};

class AsioTest : public AsioTestFixture { };

NOLINT_TEST_F(AsioTest, SmokeTest)
{
  ::Run(*io_, [&] -> Co<> {
    asio::steady_timer t(*io_);
    t.expires_after(100ms);
    const auto from = Clock::now();
    co_await t.async_wait(asio_awaitable);
    EXPECT_GE(Clock::now() - from, 90ms);
  });
}

NOLINT_TEST_F(AsioTest, AnyOf)
{
  ::Run(*io_, [&] -> Co<> {
    asio::steady_timer t1(*io_);
    asio::steady_timer t2(*io_);
    t1.expires_after(100ms);
    t2.expires_after(500ms);
    const auto from = Clock::now();
    auto [s1, s2] = co_await AnyOf(
      t1.async_wait(asio_awaitable), t2.async_wait(asio_awaitable));

    const auto d = Clock::now() - from;
    EXPECT_GE(d, 90ms);
    EXPECT_LE(d, 150ms);

    EXPECT_TRUE(s1);
    EXPECT_FALSE(s2);
  });
}

NOLINT_TEST_F(AsioTest, SleepFor)
{
  ::Run(*io_, [&] -> Co<> {
    const auto from = Clock::now();
    co_await SleepFor(*io_, 100ms);
    const auto d = Clock::now() - from;
    EXPECT_GE(d, 100ms);
  });
}

NOLINT_TEST_F(AsioTest, SocketSmoke)
{
  using asio::ip::tcp;
  ::Run(*io_, [&] -> Co<> {
    tcp::acceptor acceptor(*io_, tcp::endpoint(tcp::v4(), 8078));
    co_await AllOf(
      [&] -> Co<> {
        tcp::socket sock(*io_);
        DLOG_F(WARNING, "accepting");
        co_await acceptor.async_accept(sock, asio_awaitable);
        DLOG_F(WARNING, "accepted");
        co_await async_write(
          sock, asio::buffer("hello, world"), asio_awaitable);
      },
      [&] -> Co<> {
        tcp::socket sock(*io_);
        DLOG_F(WARNING, "connecting");
        auto endpoint = tcp::endpoint(asio::ip::address_v4(0x7f000001), 8078);
        co_await sock.async_connect(endpoint, asio_awaitable);
        DLOG_F(WARNING, "connected");
        std::array<char, 12> buf {};
        const size_t n
          = co_await async_read(sock, asio::buffer(buf), asio_awaitable);
        EXPECT_EQ(std::string(buf.data(), n), "hello, world");
      });
  });
}

class AsioThreadPoolTest : public AsioTestFixture {
public:
  static auto DoWork(const int length)
  {
    static std::atomic<uint32_t> thread_id { 0 };
    thread_local std::mt19937 rng { ++thread_id };
    const double cutoff = 1.0 / length;
    uint64_t c = 0;
    while (std::generate_canonical<double, 32>(rng) > cutoff) {
      ++c;
    }
    return c;
  }

protected:
  auto SetUp() -> void override
  {
    AsioTestFixture::SetUp();
    tp_ = std::make_unique<ThreadPool>(*io_, 4);
  }

  std::unique_ptr<ThreadPool> tp_;
};

NOLINT_TEST_F(AsioThreadPoolTest, Smoke)
{
  ::Run(*io_, [&] -> Co<> {
    const auto tid = co_await tp_->Run(
      [] -> std::thread::id { return std::this_thread::get_id(); });
    EXPECT_NE(tid, std::this_thread::get_id());
  });
}

NOLINT_TEST_F(AsioThreadPoolTest, Exception)
{
  ::Run(*io_, [&] -> Co<> {
    try {
      co_await tp_->Run([] -> void { throw std::runtime_error("Boom!"); });
    } catch (const std::exception&) {
      // Expected exception, do nothing
      (void)0;
    }
  });
}

NOLINT_TEST_F(AsioThreadPoolTest, Cancellation_Confirmed)
{
  ::Run(*io_, [&] -> Co<> {
    std::atomic<bool> confirmed { false };
    auto body = [&] -> Co<> {
      co_await tp_->Run([&](ThreadPool::CancelToken cancelled) -> void {
        while (!cancelled) { }
        confirmed = true;
      });
      ADD_FAILURE() << "should never reach here";
    };
    co_await AnyOf(body, SleepFor(*io_, 1ms));
    EXPECT_TRUE(confirmed.load());
  });
}

NOLINT_TEST_F(AsioThreadPoolTest, Cancellation_UnConfirmed)
{
  ::Run(*io_, [&] -> Co<> {
    std::atomic<bool> cancelled { false };
    auto body = [&] -> Co<int> {
      int ret = co_await tp_->Run([&](ThreadPool::CancelToken) -> int {
        while (!cancelled.load(std::memory_order_relaxed)) { }
        return 42;
      });
      // Cancel token was not consumed, so this line should get executed
      co_return ret;
    };

    auto [ret, _] = co_await AnyOf(body, [&] -> Co<> {
      co_await SleepFor(*io_, 1ms);
      cancelled = true;
    });
    EXPECT_EQ(ret, 42);
  });
}

NOLINT_TEST_F(AsioThreadPoolTest, Stress)
{
  ::Run(*io_, [&] -> Co<> {
    auto run_stress = [&](int length, int count) -> Co<uint64_t> {
      uint64_t ret = 0;
      Semaphore sem { 1000 };
      OXCO_WITH_NURSERY(n)
      {
        while (count--) {
          n.Start([&sem, &ret, this, length] -> Co<> {
            auto lk = co_await sem.Lock(); // limit concurrency
            ret += co_await tp_->Run(
              [length] -> uint64_t { return DoWork(length); });
          });
        }
        co_return kJoin;
      };
      co_return ret;
    };

    // These tasks run around 1us each
    co_await run_stress(500, 1000);
  });
}

#if defined(ASIO_HAS_FILE)

constexpr std::size_t kBlockCount = 32;
constexpr std::size_t kBlockSize = 4096;
constexpr std::string_view kHead = "0123456789";
constexpr std::string_view kTail = "ABCD";
constexpr std::uint64_t kTailOffset = 20;

//! File IO through the asio bridge, on a temporary file opened for overlapped
//! IO and bound to the test's `io_context`.
class AsioFileTest : public AsioTestFixture {
protected:
  auto SetUp() -> void override
  {
    AsioTestFixture::SetUp();
    const auto* info = testing::UnitTest::GetInstance()->current_test_info();
    path_ = std::filesystem::temp_directory_path()
      / (std::string("oxygen-oxco-asio-") + info->name() + ".bin");
  }

  auto TearDown() -> void override
  {
    std::error_code error;
    std::filesystem::remove(path_, error);
    AsioTestFixture::TearDown();
  }

  [[nodiscard]] auto OpenFile(const asio::file_base::flags flags) const
    -> asio::random_access_file
  {
    // asio takes file paths as UTF-8.
    const auto utf8 = path_.u8string();
    return { *io_, std::string(utf8.begin(), utf8.end()), flags };
  }

  //! Opens the file for reading and writing, created empty.
  [[nodiscard]] auto OpenNewFile() const -> asio::random_access_file
  {
    return OpenFile(asio::file_base::read_write | asio::file_base::create
      | asio::file_base::truncate);
  }

  //! Writes `kHead` at offset 0 and `kTail` at `kTailOffset`, leaving a
  //! zero-filled gap between them.
  static auto WriteSample(asio::random_access_file& file) -> Co<>
  {
    co_await asio::async_write_at(file, 0, asio::buffer(kHead), asio_awaitable);
    co_await asio::async_write_at(
      file, kTailOffset, asio::buffer(kTail), asio_awaitable);
  }

private:
  std::filesystem::path path_;
};

NOLINT_TEST_F(AsioFileTest, WritesLandAtTheirOffsetsAndReportTheirByteCounts)
{
  ::Run(*io_, [&] -> Co<> {
    auto file = OpenNewFile();
    const auto head_written = co_await file.async_write_some_at(
      0, asio::buffer(kHead), asio_awaitable);
    const auto tail_written = co_await file.async_write_some_at(
      kTailOffset, asio::buffer(kTail), asio_awaitable);
    EXPECT_EQ(head_written, kHead.size());
    EXPECT_EQ(tail_written, kTail.size());

    std::array<char, 4> buffer {};
    auto n = co_await file.async_read_some_at(
      3, asio::buffer(buffer), asio_awaitable);
    EXPECT_EQ(std::string_view(buffer.data(), n), "3456");
    n = co_await file.async_read_some_at(
      kTailOffset, asio::buffer(buffer), asio_awaitable);
    EXPECT_EQ(std::string_view(buffer.data(), n), kTail);
  });
}

NOLINT_TEST_F(AsioFileTest, ReadCrossingTheEndOfFileReturnsBytesUpToTheEnd)
{
  ::Run(*io_, [&] -> Co<> {
    auto file = OpenNewFile();
    co_await WriteSample(file);

    std::array<char, 16> buffer {};
    const auto n = co_await file.async_read_some_at(
      kTailOffset - 2, asio::buffer(buffer), asio_awaitable);
    EXPECT_EQ(
      std::string_view(buffer.data(), n), std::string_view("\0\0ABCD", 6));
  });
}

NOLINT_TEST_F(AsioFileTest, ReadAtTheEndOfFileReportsEofWithNoBytes)
{
  ::Run(*io_, [&] -> Co<> {
    auto file = OpenNewFile();
    co_await WriteSample(file);

    std::array<char, 16> buffer {};
    const auto [ec, n] = co_await file.async_read_some_at(
      kTailOffset + kTail.size(), asio::buffer(buffer), asio_nothrow_awaitable);
    EXPECT_EQ(ec, asio::error::eof);
    EXPECT_EQ(n, 0U);
  });
}

NOLINT_TEST_F(
  AsioFileTest, ReadsInFlightTogetherOnOneHandleEachGetTheirOwnBlock)
{
  ::Run(*io_, [&] -> Co<> {
    auto file = OpenNewFile();
    std::vector<char> content(kBlockCount * kBlockSize);
    for (std::size_t block = 0; block < kBlockCount; ++block) {
      std::fill_n(
        content.begin() + static_cast<std::ptrdiff_t>(block * kBlockSize),
        kBlockSize, static_cast<char>('a' + block));
    }
    co_await asio::async_write_at(
      file, 0, asio::buffer(content), asio_awaitable);

    std::vector<Co<>> reads;
    reads.reserve(kBlockCount);
    for (std::size_t block = 0; block < kBlockCount; ++block) {
      reads.push_back([](asio::random_access_file& f, const std::size_t b,
                        const std::thread::id loop_thread) -> Co<> {
        std::vector<char> buffer(kBlockSize);
        const auto n = co_await f.async_read_some_at(
          b * kBlockSize, asio::buffer(buffer), asio_awaitable);
        EXPECT_EQ(n, kBlockSize);
        EXPECT_EQ(std::this_thread::get_id(), loop_thread);
        EXPECT_TRUE(std::ranges::all_of(buffer, [b](const char c) -> bool {
          return c == static_cast<char>('a' + b);
        }));
      }(file, block, std::this_thread::get_id()));
    }
    co_await AllOf(std::move(reads));
  });
}

NOLINT_TEST_F(AsioFileTest, WriteToAReadOnlyFileReportsAccessDenied)
{
  ::Run(*io_, [&] -> Co<> {
    OpenNewFile().close();
    auto file = OpenFile(asio::file_base::read_only);

    const auto [ec, n] = co_await file.async_write_some_at(
      0, asio::buffer(kHead), asio_nothrow_awaitable);
#  if defined(OXYGEN_WINDOWS)
    EXPECT_EQ(ec,
      asio::error_code(
        ERROR_ACCESS_DENIED, asio::error::get_system_category()));
#  else
    EXPECT_EQ(ec, asio::error::access_denied);
#  endif
    EXPECT_EQ(n, 0U);
  });
}

NOLINT_TEST_F(AsioFileTest, ReadOnAClosedFileReportsBadDescriptor)
{
  ::Run(*io_, [&] -> Co<> {
    auto file = OpenNewFile();
    file.close();

    std::array<char, 4> buffer {};
    const auto [ec, n] = co_await file.async_read_some_at(
      0, asio::buffer(buffer), asio_nothrow_awaitable);
    EXPECT_EQ(ec, asio::error::bad_descriptor);
    EXPECT_EQ(n, 0U);
  });
}

NOLINT_TEST_F(AsioFileTest, ReadOnAClosedFileThrowsThroughTheThrowingToken)
{
  ::Run(*io_, [&] -> Co<> {
    auto file = OpenNewFile();
    file.close();

    std::array<char, 4> buffer {};
    try {
      co_await file.async_read_some_at(0, asio::buffer(buffer), asio_awaitable);
      ADD_FAILURE() << "a read on a closed file must throw";
    } catch (const asio::system_error& error) {
      EXPECT_EQ(error.code(), asio::error::bad_descriptor);
    }
  });
}

#endif // ASIO_HAS_FILE

#if defined(OXYGEN_WINDOWS) && defined(ASIO_HAS_FILE)

//! A pipe whose reads stay in the kernel until its server end writes.
/*!
 The client end is a `random_access_file`: asio opens it with the same
 `CreateFileW` and IOCP registration as a disk file, so a read on it is an
 overlapped file operation that stays pending for as long as a test needs.
*/
class PendingPipe {
public:
  PendingPipe(asio::io_context& io, const std::string& name)
    : server_(::CreateNamedPipeA(name.c_str(), PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr))
    , client_(io, name, asio::file_base::read_write)
  {
  }

  ~PendingPipe() { ::CloseHandle(server_); }

  OXYGEN_MAKE_NON_COPYABLE(PendingPipe)
  OXYGEN_MAKE_NON_MOVABLE(PendingPipe)

  auto Write(const std::string_view data) const -> void
  {
    DWORD written = 0;
    EXPECT_TRUE(::WriteFile(server_, data.data(),
      static_cast<DWORD>(data.size()), &written, nullptr));
  }

  auto Client() -> asio::random_access_file& { return client_; }

private:
  HANDLE server_;
  asio::random_access_file client_;
};

class AsioPipeTest : public AsioTestFixture {
protected:
  auto SetUp() -> void override
  {
    AsioTestFixture::SetUp();
    const auto* info = testing::UnitTest::GetInstance()->current_test_info();
    pipe_ = std::make_unique<PendingPipe>(*io_,
      std::string(R"(\\.\pipe\oxygen-oxco-asio-)") + info->name() + "-"
        + std::to_string(::GetCurrentProcessId()));
  }

  auto TearDown() -> void override
  {
    pipe_.reset();
    AsioTestFixture::TearDown();
  }

  //! Runs whatever completion is still queued after `co::Run` returned.
  /*!
   Returns the number of handlers run. `poll()` stops an `io_context` that has
   no outstanding work, so the context is restarted for the next `co::Run`.
  */
  [[nodiscard]] auto RunQueuedCompletions() const -> std::size_t
  {
    const auto count = io_->poll();
    io_->restart();
    return count;
  }

  std::unique_ptr<PendingPipe> pipe_;
};

NOLINT_TEST_F(AsioPipeTest, ReadLosingAnyOfToATimerIsAbortedBeforeAnyOfResumes)
{
  auto& client = pipe_->Client();
  ::Run(*io_, [&] -> Co<> {
    bool read_frame_alive = false;
    auto read = [&] -> Co<> {
      std::array<char, 16> buffer {};
      read_frame_alive = true;
      ScopeGuard gone([&] noexcept -> void { read_frame_alive = false; });
      co_await client.async_read_some_at(
        0, asio::buffer(buffer), asio_awaitable);
      ADD_FAILURE() << "the read must not complete";
    };
    const auto [completed, slept] = co_await AnyOf(read, SleepFor(*io_, 20ms));
    EXPECT_FALSE(completed);
    EXPECT_TRUE(slept);
    EXPECT_FALSE(read_frame_alive);
  });
  // AnyOf resumed only after the aborted completion ran: none is left queued
  // for a frame that no longer exists.
  EXPECT_EQ(RunQueuedCompletions(), 0U);
}

NOLINT_TEST_F(AsioPipeTest, DataArrivingAfterACancelledReadGoesToTheNextRead)
{
  auto& client = pipe_->Client();
  ::Run(*io_, [&] -> Co<> {
    std::array<char, 16> buffer {};
    co_await AnyOf(
      client.async_read_some_at(0, asio::buffer(buffer), asio_awaitable),
      SleepFor(*io_, 20ms));
  });

  // The kernel no longer holds the cancelled read.
  pipe_->Write("late");
  ::Run(*io_, [&] -> Co<> {
    std::array<char, 16> buffer {};
    const auto n = co_await client.async_read_some_at(
      0, asio::buffer(buffer), asio_awaitable);
    EXPECT_EQ(std::string_view(buffer.data(), n), "late");
  });
}

NOLINT_TEST_F(
  AsioPipeTest, CancelledNurseryJoinsOnlyAfterEveryPendingReadIsAborted)
{
  constexpr int kReads = 4;
  auto& client = pipe_->Client();
  int pending = 0;
  ::Run(*io_, [&] -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      for (int i = 0; i < kReads; ++i) {
        n.Start([&] -> Co<> {
          std::array<char, 16> buffer {};
          ++pending;
          ScopeGuard done([&] noexcept -> void { --pending; });
          co_await client.async_read_some_at(
            0, asio::buffer(buffer), asio_awaitable);
          ADD_FAILURE() << "the read must not complete";
        });
      }
      co_await SleepFor(*io_, 20ms);
      EXPECT_EQ(pending, kReads);
      co_return kCancel;
    };
    // The nursery joined after every read was aborted and its frame destroyed.
    EXPECT_EQ(pending, 0);
  });
  EXPECT_EQ(RunQueuedCompletions(), 0U);
}

#endif // OXYGEN_WINDOWS && ASIO_HAS_FILE

#if defined(ASIO_HAS_WINDOWS_OBJECT_HANDLE)

class AsioEventTest : public AsioTestFixture {
protected:
  [[nodiscard]] auto MakeEvent() const -> asio::windows::object_handle
  {
    return { *io_, ::CreateEventW(nullptr, FALSE, FALSE, nullptr) };
  }
};

NOLINT_TEST_F(AsioEventTest, SettingTheEventResumesTheWaiterOnTheLoopThread)
{
  auto event = MakeEvent();
  ::Run(*io_, [&] -> Co<> {
    const auto loop_thread = std::this_thread::get_id();
    co_await AllOf(
      [&] -> Co<> {
        co_await event.async_wait(asio_awaitable);
        EXPECT_EQ(std::this_thread::get_id(), loop_thread);
      },
      [&] -> Co<> {
        co_await SleepFor(*io_, 10ms);
        EXPECT_TRUE(::SetEvent(event.native_handle()));
      });
  });
}

NOLINT_TEST_F(
  AsioEventTest, CancellingTheHandleResumesTheWaiterWithOperationAborted)
{
  auto event = MakeEvent();
  ::Run(*io_, [&] -> Co<> {
    co_await AllOf(
      [&] -> Co<> {
        const auto ec = co_await event.async_wait(asio_nothrow_awaitable);
        EXPECT_TRUE(IsOperationAborted(ec));
      },
      [&] -> Co<> {
        co_await SleepFor(*io_, 10ms);
        event.cancel();
      });
  });
}

//! An `object_handle` wait has no per-operation cancellation slot: cancelling
//! the awaiting task leaves the wait pending until `cancel()` or the event.
NOLINT_TEST_F(AsioEventTest,
  CancellingTheWaitingTaskLeavesTheWaitPendingUntilTheHandleIsCancelled)
{
  auto event = MakeEvent();
  ::Run(*io_, [&] -> Co<> {
    Clock::duration any_of_took {};
    co_await AllOf(
      [&] -> Co<> {
        const auto from = Clock::now();
        co_await AnyOf(event.async_wait(asio_awaitable), SleepFor(*io_, 10ms));
        any_of_took = Clock::now() - from;
      },
      [&] -> Co<> {
        co_await SleepFor(*io_, 60ms);
        event.cancel();
      });
    EXPECT_GE(any_of_took, 55ms);
  });
}

#endif // ASIO_HAS_WINDOWS_OBJECT_HANDLE

} // namespace
