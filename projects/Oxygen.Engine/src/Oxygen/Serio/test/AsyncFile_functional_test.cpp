//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Serio/IoDriver.h, Serio/AsyncFile.h, Serio/IoDriver.cpp,
// Serio/Platform/*/IocpBackend.cpp, Serio/Platform/*/UringBackend.cpp,
// Serio/CopyRegion.h

#include <bit>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "Support/DeviceFactories.h"
#include <asio/io_context.hpp>

#include <Oxygen/Base/Platforms.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/asio.h> // IWYU pragma: keep (EventLoopTraits)
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/AsyncFile.h>
#include <Oxygen/Serio/CopyRegion.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/IoDriver.h>
#include <Oxygen/Serio/Region.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>

#ifdef OXYGEN_WINDOWS
#  include <Windows.h> // IWYU pragma: keep
#endif

namespace {

using namespace std::chrono_literals;
using oxygen::Result;
using oxygen::serio::Access;
using oxygen::serio::AnyAsyncDevice;
using oxygen::serio::AsyncFile;
using oxygen::serio::ByteCount;
using oxygen::serio::ByteOffset;
using oxygen::serio::ByteRange;
using oxygen::serio::CopyRegion;
using oxygen::serio::Error;
using oxygen::serio::IoDriver;
using oxygen::serio::kIoAlignment;
using oxygen::serio::ReadExactlyAt;
using oxygen::serio::Region;
using oxygen::serio::test::Required;
using oxygen::serio::test::TempFiles;
using oxygen::serio::testing::BytesEqual;
using oxygen::serio::testing::PatternBytes;
namespace co = oxygen::co;

//! The completion mechanism of the driver a case runs under.
enum class DriverKind : uint8_t { kLoop, kInline };

//! A driver of the given kind with its loop and temporary files.
class DriverHarness {
public:
  explicit DriverHarness(const DriverKind kind)
    : driver_(kind == DriverKind::kInline
          ? Required(IoDriver::CreateInline(), "IoDriver::CreateInline")
          : Required(IoDriver::Create(loop_), "IoDriver::Create"))
  {
  }

protected:
  auto Loop() -> asio::io_context& { return loop_; }
  auto Driver() -> IoDriver& { return driver_; }
  auto Files() -> TempFiles& { return files_; }

  auto OpenWritten(const std::span<const std::byte> bytes,
    const Access access = Access::kRead) -> AsyncFile
  {
    return Required(
      driver_.Open(files_.Write(bytes), { .access = access }), "open");
  }

  static auto ReadAll(const AsyncFile& file) -> std::vector<std::byte>
  {
    auto bytes = std::vector<std::byte>(
      static_cast<size_t>(Required(file.Size(), "Size").get()));
    Required(ReadExactlyAt(file, ByteOffset { 0 }, bytes), "read");
    return bytes;
  }

private:
  asio::io_context loop_;
  IoDriver driver_;
  TempFiles files_;
};

//! Awaited IO is in the async device conformance suite; these cases cover
//! what only a driver adds: opening, OS errors, buffers, large transfers,
//! copies between files and cancellation. Each case runs under a loop driver
//! and under an inline driver.
class AsyncFileTest : public ::testing::TestWithParam<DriverKind>,
                      protected DriverHarness {
protected:
  AsyncFileTest()
    : DriverHarness(GetParam())
  {
  }
};

INSTANTIATE_TEST_SUITE_P(Serio, AsyncFileTest,
  ::testing::Values(DriverKind::kLoop, DriverKind::kInline),
  [](const ::testing::TestParamInfo<DriverKind>& info) {
    return info.param == DriverKind::kInline ? std::string("Inline")
                                             : std::string("Loop");
  });

//! The cases only an inline driver has.
class InlineDriverTest : public ::testing::Test, protected DriverHarness {
protected:
  InlineDriverTest()
    : DriverHarness(DriverKind::kInline)
  {
  }
};

//! Reads `into.size()` bytes at `at` of `file`, expecting them all.
auto ReadChunk(AsyncFile& file, const size_t at, std::span<std::byte> into)
  -> co::Co<>
{
  const auto read = co_await file.AsyncReadAt(ByteOffset { at }, into);
  EXPECT_TRUE(read);
}

//! Concurrent reads: `count` reads of `size` bytes, the n-th at
//! `n * stride` in the file.
struct ReadSpread {
  size_t count;
  size_t size;
  size_t stride;
};

//! Starts the reads `spread` describes at once, the n-th into the n-th
//! `size` bytes of `out`, and waits for all of them.
auto ReadConcurrently(AsyncFile& file, std::span<std::byte> out,
  const ReadSpread spread) -> co::Co<>
{
  OXCO_WITH_NURSERY(nursery)
  {
    for (size_t index = 0; index < spread.count; ++index) {
      nursery.Start(&ReadChunk, std::ref(file), index * spread.stride,
        out.subspan(index * spread.size, spread.size));
    }
    co_return co::kJoin;
  };
}

//! Opening a missing file through the driver fails with kNotFound.
NOLINT_TEST_P(AsyncFileTest, OpenMissingIsNotFound)
{
  const auto file = Driver().Open(Files().Path("missing.bin"), {});

  ASSERT_FALSE(file);
  EXPECT_EQ(file.error(), Error::kNotFound);
}

//! An awaited write to a file opened for reading fails with kAccessDenied.
NOLINT_TEST_P(AsyncFileTest, AwaitedWriteToReadOnlyFileIsAccessDenied)
{
  const auto bytes = PatternBytes(64);
  auto file = OpenWritten(bytes);

  const auto written = co::Run(Loop(), [&] -> co::Co<Result<void>> {
    co_return co_await file.AsyncWriteAt(ByteOffset { 0 }, bytes);
  }());

  ASSERT_FALSE(written);
  EXPECT_EQ(written.error(), Error::kAccessDenied);
}

//! The driver's buffers have the size asked for and serve unbuffered IO.
NOLINT_TEST_P(AsyncFileTest, AllocatedBufferIsAligned)
{
  const auto buffer = Driver().AllocateBuffer(ByteCount { 10000 });

  ASSERT_TRUE(buffer);
  EXPECT_EQ(buffer->Size().get(), 10000U);
  EXPECT_EQ(
    std::bit_cast<uintptr_t>(buffer->Bytes().data()) % kIoAlignment, 0U);
}

//! A large awaited read and write each complete whole, and the coroutine
//! resumes on the loop thread.
NOLINT_TEST_P(AsyncFileTest, LargeTransfersCompleteOnTheLoopThread)
{
  constexpr size_t kSize = (3U << 20U) + 517;
  const auto bytes = PatternBytes(kSize);
  auto file = OpenWritten({}, Access::kReadWrite);
  auto out = std::vector<std::byte>(kSize);
  const auto loop_thread = std::this_thread::get_id();

  auto resumed_on = std::vector<std::thread::id> {};

  const auto [written, read] = co::Run(
    Loop(), [&] -> co::Co<std::pair<Result<void>, Result<ByteCount>>> {
      const auto write = co_await file.AsyncWriteAt(ByteOffset { 0 }, bytes);
      resumed_on.push_back(std::this_thread::get_id());
      const auto read = co_await file.AsyncReadAt(ByteOffset { 0 }, out);
      resumed_on.push_back(std::this_thread::get_id());
      co_return std::pair { write, read };
    }());

  ASSERT_TRUE(written);
  ASSERT_TRUE(read);
  EXPECT_EQ(read->get(), kSize);
  EXPECT_THAT(out, BytesEqual(bytes));
  EXPECT_THAT(resumed_on, ::testing::Each(loop_thread));
}

//! Many concurrent reads of one file all complete, each with its own bytes.
NOLINT_TEST_P(AsyncFileTest, ManyConcurrentReadsComplete)
{
  constexpr size_t kReads = 1500;
  constexpr size_t kReadSize = 64;
  const auto bytes = PatternBytes(kReads * kReadSize);
  auto file = OpenWritten(bytes);
  auto out = std::vector<std::byte>(bytes.size());

  co::Run(Loop(),
    ReadConcurrently(
      file, out, { .count = kReads, .size = kReadSize, .stride = kReadSize }));

  EXPECT_THAT(out, BytesEqual(bytes));
}

//! A copy between files overlaps chunks and writes every byte at its offset.
NOLINT_TEST_P(AsyncFileTest, CopyRegionCopiesBetweenFiles)
{
  constexpr size_t kSourceSize = (5U << 19U) + 1001;
  const auto bytes = PatternBytes(kSourceSize);
  const auto source = OpenWritten(bytes);
  auto target = OpenWritten({}, Access::kReadWrite);
  const auto range = ByteRange {
    .offset = ByteOffset { 333 },
    .count = ByteCount { kSourceSize - 1000 },
  };
  const auto region
    = Required(Region<AsyncFile>::Make(source, range), "region");

  const auto copied
    = co::Run(Loop(), CopyRegion(region, target, ByteOffset { 100 }));

  ASSERT_TRUE(copied);
  const auto written = ReadAll(target);
  ASSERT_EQ(written.size(), 100 + range.count.get());
  EXPECT_THAT(std::span(written).subspan(100),
    BytesEqual(std::span(bytes).subspan(333, range.count.get())));
}

//! A loop driver is not inline; the inline one is.
NOLINT_TEST(IoDriverKindTest, InlineIsTrueOnlyForTheInlineDriver)
{
  asio::io_context loop;
  const auto looped = Required(IoDriver::Create(loop), "IoDriver::Create");
  const auto inlined
    = Required(IoDriver::CreateInline(), "IoDriver::CreateInline");

  EXPECT_FALSE(looped.Inline());
  EXPECT_TRUE(inlined.Inline());
}

//! An inline driver's awaited operations complete when submitted: the awaiter
//! is told not to suspend, and the result is ready.
NOLINT_TEST_F(InlineDriverTest, AwaitedOperationsCompleteWithoutSuspending)
{
  const auto bytes = PatternBytes(256);
  auto file = OpenWritten(bytes, Access::kReadWrite);
  auto out = std::vector<std::byte>(64);
  const auto patch
    = PatternBytes(16, oxygen::serio::testing::PatternSeed::kSecond);

  auto read = file.AsyncReadAt(ByteOffset { 10 }, out);
  const auto read_suspends = read.await_suspend(std::noop_coroutine());
  const auto read_result = read.await_resume();
  auto write = file.AsyncWriteAt(ByteOffset { 0 }, patch);
  const auto write_suspends = write.await_suspend(std::noop_coroutine());
  const auto write_result = write.await_resume();

  EXPECT_FALSE(read_suspends);
  ASSERT_TRUE(read_result);
  EXPECT_EQ(read_result->get(), out.size());
  EXPECT_THAT(out, BytesEqual(std::span(bytes).subspan(10, out.size())));
  EXPECT_FALSE(write_suspends);
  ASSERT_TRUE(write_result);
  const auto written = ReadAll(file);
  EXPECT_THAT(std::span(written).first(patch.size()), BytesEqual(patch));
}

//! A file of an inline driver erases into an AnyAsyncDevice, which awaits
//! reads and writes through it.
NOLINT_TEST_F(InlineDriverTest, FileErasesIntoAnAsyncDevice)
{
  const auto bytes = PatternBytes(512);
  auto device = AnyAsyncDevice(OpenWritten(bytes, Access::kReadWrite));
  auto out = std::vector<std::byte>(100);
  const auto patch
    = PatternBytes(8, oxygen::serio::testing::PatternSeed::kSecond);

  const auto read = co::Run(Loop(), [&] -> co::Co<Result<ByteCount>> {
    co_return co_await device.AsyncReadAt(ByteOffset { 200 }, out);
  }());
  const auto written = co::Run(Loop(), [&] -> co::Co<Result<void>> {
    co_return co_await device.AsyncWriteAt(ByteOffset { 0 }, patch);
  }());

  ASSERT_TRUE(read);
  EXPECT_THAT(out, BytesEqual(std::span(bytes).subspan(200, out.size())));
  ASSERT_TRUE(written);
  auto head = std::vector<std::byte>(patch.size());
  ASSERT_TRUE(ReadExactlyAt(device, ByteOffset { 0 }, head));
  EXPECT_THAT(head, BytesEqual(patch));
}

//! A file of an inline driver has regions, whose reads stay inside them.
NOLINT_TEST_F(InlineDriverTest, FileHasRegions)
{
  const auto bytes = PatternBytes(512);
  const auto file = OpenWritten(bytes);
  const auto region = Required(
    Region<AsyncFile>::Make(file,
      ByteRange { .offset = ByteOffset { 100 }, .count = ByteCount { 50 } }),
    "region");
  auto out = std::vector<std::byte>(80);

  const auto read = co::Run(Loop(), [&] -> co::Co<Result<ByteCount>> {
    co_return co_await region.AsyncReadAt(ByteOffset { 0 }, out);
  }());

  ASSERT_TRUE(read);
  ASSERT_EQ(read->get(), 50U);
  EXPECT_THAT(
    std::span(out).first(50), BytesEqual(std::span(bytes).subspan(100, 50)));
}

#ifdef OXYGEN_WINDOWS

//! A named pipe opened through the driver: its reads stay pending in the
//! kernel until the server end writes, so a test can cancel one.
class AsyncPipeTest : public ::testing::Test, protected DriverHarness {
protected:
  AsyncPipeTest()
    : DriverHarness(DriverKind::kLoop)
  {
  }

  auto SetUp() -> void override
  {
    const auto* const info
      = ::testing::UnitTest::GetInstance()->current_test_info();
    const auto name = std::string(R"(\\.\pipe\oxygen-serio-)") + info->name()
      + "-" + std::to_string(::GetCurrentProcessId());
    server_ = ::CreateNamedPipeA(name.c_str(), PIPE_ACCESS_DUPLEX,
      PIPE_TYPE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr);
    ASSERT_NE(server_, INVALID_HANDLE_VALUE);
    client_.emplace(Required(
      Driver().Open(name, { .access = Access::kReadWrite }), "open pipe"));
  }

  auto TearDown() -> void override
  {
    client_.reset();
    if (server_ != INVALID_HANDLE_VALUE) {
      ::CloseHandle(server_);
    }
  }

  auto Write(const std::string_view data) const -> void
  {
    DWORD written = 0;
    EXPECT_TRUE(::WriteFile(server_, data.data(),
      static_cast<DWORD>(data.size()), &written, nullptr));
  }

  //! The pipe's client end; empty when `SetUp` failed to open it.
  auto Client() -> std::optional<AsyncFile>& { return client_; }

private:
  HANDLE server_ { INVALID_HANDLE_VALUE };
  std::optional<AsyncFile> client_;
};

//! A read that loses an AnyOf is aborted, and AnyOf resumes only once the
//! OS has released its buffer: no completion is left for a dead frame.
NOLINT_TEST_F(AsyncPipeTest, CancelledReadEndsAfterTheOsReleasesItsBuffer)
{
  auto& client = Client();
  ASSERT_HAS_VALUE(client);
  auto& file = *client;

  co::Run(Loop(), [&] -> co::Co<> {
    auto buffer = std::vector<std::byte>(4);
    const auto [read, slept] = co_await co::AnyOf(
      file.AsyncReadAt(ByteOffset { 0 }, buffer), co::SleepFor(Loop(), 20ms));
    EXPECT_FALSE(read);
    EXPECT_TRUE(slept);
  }());

  EXPECT_EQ(Loop().poll(), 0U);
  Loop().restart();
}

//! Many pending reads cancelled together all end, and none leaves a
//! completion behind.
NOLINT_TEST_F(AsyncPipeTest, ManyCancelledReadsAllEnd)
{
  auto& client = Client();
  ASSERT_HAS_VALUE(client);
  auto& file = *client;
  constexpr size_t kReads = 600;
  auto out = std::vector<std::byte>(kReads * 4);

  co::Run(Loop(), [&] -> co::Co<> {
    const auto [read, slept] = co_await co::AnyOf(
      ReadConcurrently(file, out, { .count = kReads, .size = 4, .stride = 0 }),
      co::SleepFor(Loop(), 50ms));
    EXPECT_FALSE(read);
    EXPECT_TRUE(slept);
  }());

  EXPECT_EQ(Loop().poll(), 0U);
  Loop().restart();
}

//! Bytes that arrive after a cancelled read go to the next read.
NOLINT_TEST_F(AsyncPipeTest, DataAfterACancelledReadGoesToTheNextRead)
{
  auto& client = Client();
  ASSERT_HAS_VALUE(client);
  auto& file = *client;

  co::Run(Loop(), [&] -> co::Co<> {
    auto buffer = std::vector<std::byte>(4);
    co_await co::AnyOf(
      file.AsyncReadAt(ByteOffset { 0 }, buffer), co::SleepFor(Loop(), 20ms));
  }());

  Write("late");
  const auto received = co::Run(Loop(), [&] -> co::Co<std::string> {
    auto buffer = std::vector<std::byte>(4);
    const auto read = co_await file.AsyncReadAt(ByteOffset { 0 }, buffer);
    EXPECT_TRUE(read);
    auto text = std::string {};
    for (const auto byte : buffer) {
      text.push_back(static_cast<char>(byte));
    }
    co_return text;
  }());

  EXPECT_EQ(received, "late");
}

#endif // OXYGEN_WINDOWS

} // namespace
