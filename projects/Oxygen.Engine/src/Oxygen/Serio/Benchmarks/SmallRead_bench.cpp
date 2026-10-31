//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Small-read operations per second: reads scattered over one file, awaited
// through the platform's IO driver by 1 to 1024 concurrent readers, against
// the same reads made blocking, one after another.
//
// - Cached: descriptor-sized reads of a file just written, served from the
//   OS cache, measuring each path's per-operation cost.
// - Uncached: 4 KiB reads that bypass the OS cache (unbuffered), so each
//   waits on the device, measuring how reads in flight overlap.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <asio/io_context.hpp>
#include <benchmark/benchmark.h>

#include <Oxygen/Base/Result.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/asio.h> // IWYU pragma: keep (EventLoopTraits)
#include <Oxygen/Serio/AsyncFile.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/IoDriver.h>
#include <Oxygen/Serio/NativeFile.h>
#include <Oxygen/Serio/Types.h>

namespace {

namespace co = oxygen::co;
using oxygen::serio::AsyncFile;
using oxygen::serio::ByteBuffer;
using oxygen::serio::ByteCount;
using oxygen::serio::ByteOffset;
using oxygen::serio::IoDriver;
using oxygen::serio::kIoAlignment;
using oxygen::serio::NativeFile;
using oxygen::serio::OpenSpec;

constexpr size_t kFileSize = size_t { 64 } << 20U;

//! Descriptor-sized reads from the OS cache.
struct Cached {
  static constexpr size_t kReadSize = 256;
  //! Reads per benchmark iteration, split among the concurrent readers.
  static constexpr size_t kReads = 8192;
  static constexpr bool kUnbuffered = false;
};

//! Unbuffered reads of the smallest size the device takes.
struct Uncached {
  static constexpr size_t kReadSize = kIoAlignment;
  static constexpr size_t kReads = 2048;
  static constexpr bool kUnbuffered = true;
};

//! A file of `kFileSize` bytes in the temp directory, removed at exit.
class BenchFile {
public:
  BenchFile()
    : path_(
        std::filesystem::temp_directory_path() / "oxygen-serio-small-read.bin")
  {
    auto bytes = std::vector<char>(kFileSize);
    for (size_t index = 0; index < bytes.size(); ++index) {
      bytes[index] = static_cast<char>((index * 31U) & 0xFFU);
    }
    auto out = std::ofstream(path_, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
      throw std::runtime_error("cannot write " + path_.string());
    }
  }

  ~BenchFile()
  {
    auto error = std::error_code {};
    std::filesystem::remove(path_, error);
  }

  BenchFile(const BenchFile&) = delete;
  BenchFile(BenchFile&&) = delete;
  auto operator=(const BenchFile&) -> BenchFile& = delete;
  auto operator=(BenchFile&&) -> BenchFile& = delete;

  [[nodiscard]] auto Path() const -> const std::filesystem::path&
  {
    return path_;
  }

  static auto Get() -> const BenchFile&
  {
    static const auto file = BenchFile {};
    return file;
  }

private:
  std::filesystem::path path_;
};

template <class T>
auto Required(oxygen::Result<T> result, const char* what) -> T
{
  if (!result) {
    throw std::runtime_error(
      std::string(what) + ": " + result.error().message());
  }
  return std::move(*result);
}

//! The offset of the `index`-th read of reader `reader`, a multiple of the
//! read size scattered over the file, the same for awaited and blocking
//! reads.
template <class Pattern>
auto OffsetOf(const size_t reader, const size_t index) -> ByteOffset
{
  auto mixed = (uint64_t { reader } << 32U) ^ index;
  mixed *= 0x9E37'79B9'7F4A'7C15ULL;
  mixed ^= mixed >> 29U;
  constexpr auto kSlots = kFileSize / Pattern::kReadSize;
  return ByteOffset { (mixed % kSlots) * Pattern::kReadSize };
}

//! A read buffer, aligned for unbuffered reads.
template <class Pattern> auto ReadBuffer() -> ByteBuffer
{
  return Required(
    ByteBuffer::Allocate(ByteCount { Pattern::kReadSize }), "buffer");
}

template <class Pattern> auto OpenSpecOf() -> OpenSpec
{
  return { .unbuffered = Pattern::kUnbuffered };
}

//! A reader's part of an iteration: its index and its number of reads.
struct ReaderShare {
  size_t reader;
  size_t reads;
};

//! One reader: its reads one after another into its own buffer.
template <class Pattern>
auto Reader(AsyncFile& file, const ReaderShare share) -> co::Co<>
{
  auto buffer = ReadBuffer<Pattern>();
  for (size_t index = 0; index < share.reads; ++index) {
    auto read = co_await file.AsyncReadAt(
      OffsetOf<Pattern>(share.reader, index), buffer.Bytes());
    benchmark::DoNotOptimize(read);
  }
}

template <class Pattern>
auto ReadConcurrently(AsyncFile& file, const size_t readers) -> co::Co<>
{
  OXCO_WITH_NURSERY(nursery)
  {
    for (size_t reader = 0; reader < readers; ++reader) {
      nursery.Start(&Reader<Pattern>, std::ref(file),
        ReaderShare { .reader = reader, .reads = Pattern::kReads / readers });
    }
    co_return co::kJoin;
  };
}

//! Awaited reads through the driver, `state.range(0)` readers at a time.
template <class Pattern> auto BM_AwaitedReads(benchmark::State& state) -> void
{
  const auto readers = static_cast<size_t>(state.range(0));
  asio::io_context loop;
  auto driver = Required(IoDriver::Create(loop), "IoDriver::Create");
  auto file = Required(
    driver.Open(BenchFile::Get().Path(), OpenSpecOf<Pattern>()), "Open");
  for (auto _ : state) {
    co::Run(loop, ReadConcurrently<Pattern>(file, readers));
  }
  state.SetItemsProcessed(
    static_cast<int64_t>(state.iterations() * Pattern::kReads));
}

//! The same reads, blocking, one after another on the calling thread.
template <class Pattern> auto BM_BlockingReads(benchmark::State& state) -> void
{
  auto file = Required(
    NativeFile::Open(BenchFile::Get().Path(), OpenSpecOf<Pattern>()), "Open");
  auto buffer = ReadBuffer<Pattern>();
  for (auto _ : state) {
    for (size_t index = 0; index < Pattern::kReads; ++index) {
      auto read = file.ReadAt(OffsetOf<Pattern>(0, index), buffer.Bytes());
      benchmark::DoNotOptimize(read);
    }
  }
  state.SetItemsProcessed(
    static_cast<int64_t>(state.iterations() * Pattern::kReads));
}

} // namespace

// Rates are per wall-clock second: a read waiting on the device costs no CPU.
BENCHMARK_TEMPLATE(BM_BlockingReads, Cached)->UseRealTime();
BENCHMARK_TEMPLATE(BM_AwaitedReads, Cached)
  ->RangeMultiplier(4)
  ->Range(1, 1024)
  ->UseRealTime();
BENCHMARK_TEMPLATE(BM_BlockingReads, Uncached)->UseRealTime();
BENCHMARK_TEMPLATE(BM_AwaitedReads, Uncached)
  ->RangeMultiplier(4)
  ->Range(1, 1024)
  ->UseRealTime();
