//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <thread>

#ifdef _WIN32
#  include <Windows.h>
#endif

#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Engine/Scripting/IScriptCompiler.h>
#include <Oxygen/Engine/Scripting/ScriptCompilationService.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/OxCo/ThreadPool.h>

using namespace std::chrono_literals;

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
} // namespace oxygen::engine::internal

namespace {

using oxygen::observer_ptr;
using oxygen::co::AllOf;
using oxygen::co::Co;
using oxygen::co::ThreadPool;
using oxygen::co::testing::TestEventLoop;
using oxygen::core::meta::scripting::ScriptCompileMode;
using oxygen::data::pak::scripting::ScriptLanguage;
using oxygen::scripting::IScriptCompiler;
using oxygen::scripting::ScriptBytecodeBlob;
using oxygen::scripting::ScriptCompilationService;
using oxygen::scripting::ScriptCompileResult;
using oxygen::scripting::ScriptSourceBlob;

auto MakeSourceBlob(std::vector<uint8_t> bytes) -> ScriptSourceBlob
{
  return ScriptSourceBlob::FromOwned(std::move(bytes), ScriptLanguage::kLuau,
    oxygen::data::pak::scripting::ScriptCompression::kNone, 0,
    oxygen::scripting::ScriptBlobOrigin::kEmbeddedResource,
    oxygen::scripting::ScriptBlobCanonicalName { "test-script" });
}

auto MakeBytecodeBlob(std::vector<uint8_t> bytes)
  -> std::shared_ptr<const ScriptBytecodeBlob>
{
  return std::make_shared<const ScriptBytecodeBlob>(
    ScriptBytecodeBlob::FromOwned(std::move(bytes), ScriptLanguage::kLuau,
      oxygen::data::pak::scripting::ScriptCompression::kNone, 0,
      oxygen::scripting::ScriptBlobOrigin::kEmbeddedResource,
      oxygen::scripting::ScriptBlobCanonicalName { "test-bytecode" }));
}

class CountingCompiler final : public IScriptCompiler {
public:
  explicit CountingCompiler(std::atomic<int>& compile_calls)
    : compile_calls_(compile_calls)
  {
  }

  [[nodiscard]] auto Language() const noexcept -> ScriptLanguage override
  {
    return ScriptLanguage::kLuau;
  }

  [[nodiscard]] auto Compile(ScriptSourceBlob source,
    const ScriptCompileMode /*mode*/) const -> ScriptCompileResult override
  {
    (void)source;
    ++compile_calls_;
    ScriptCompileResult result {};
    result.success = true;
    result.bytecode = MakeBytecodeBlob({ 0xCA, 0xFE, 0xBA, 0xBE }); // NOLINT
    std::this_thread::sleep_for(2ms);
    return result;
  }

private:
  std::atomic<int>& compile_calls_;
};

class ImmediateCountingCompiler final : public IScriptCompiler {
public:
  explicit ImmediateCountingCompiler(std::atomic<int>& compile_calls)
    : compile_calls_(compile_calls)
  {
  }

  [[nodiscard]] auto Language() const noexcept -> ScriptLanguage override
  {
    return ScriptLanguage::kLuau;
  }

  [[nodiscard]] auto Compile(ScriptSourceBlob /*source*/,
    const ScriptCompileMode /*mode*/) const -> ScriptCompileResult override
  {
    ++compile_calls_;
    ScriptCompileResult result {};
    result.success = true;
    result.bytecode = MakeBytecodeBlob({ 0xAB, 0xCD }); // NOLINT
    return result;
  }

private:
  std::atomic<int>& compile_calls_;
};

class SourceSizedCountingCompiler final : public IScriptCompiler {
public:
  explicit SourceSizedCountingCompiler(std::atomic<int>& compile_calls)
    : compile_calls_(compile_calls)
  {
  }

  [[nodiscard]] auto Language() const noexcept -> ScriptLanguage override
  {
    return ScriptLanguage::kLuau;
  }

  [[nodiscard]] auto Compile(ScriptSourceBlob source,
    const ScriptCompileMode /*mode*/) const -> ScriptCompileResult override
  {
    ++compile_calls_;
    auto source_bytes = source.BytesView();
    std::vector<uint8_t> bytecode(source_bytes.begin(), source_bytes.end());
    bytecode.push_back(0xEE); // NOLINT
    ScriptCompileResult result {};
    result.success = true;
    result.bytecode = MakeBytecodeBlob(std::move(bytecode));
    return result;
  }

private:
  std::atomic<int>& compile_calls_;
};

// NOLINTBEGIN(*-magic-numbers)

NOLINT_TEST(ScriptCompilationServiceTest, MissingCompilerReturnsFailure)
{
  TestEventLoop loop;
  ScriptCompilationService service(observer_ptr<ThreadPool> {});

  // NOLINTNEXTLINE(*capturing-lambda-*)
  oxygen::co::Run(loop, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&ScriptCompilationService::ActivateAsync, &service);
      service.Run();

      ScriptCompilationService::Request request {
        .compile_key = ScriptCompilationService::CompileKey { 123 },
        .source = MakeSourceBlob({ 1, 2, 3, 4 }),
      };

      const auto result = co_await service.CompileAsync(std::move(request));
      EXPECT_FALSE(result.success);
      EXPECT_FALSE(result.diagnostics.empty());

      service.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

NOLINT_TEST(
  ScriptCompilationServiceTest, ConcurrentSameKeyDedupesInFlightCompile)
{
  TestEventLoop loop;
  ThreadPool pool(loop, 2);

  std::atomic<int> compile_calls = 0;
  ScriptCompilationService service(observer_ptr { &pool });
  EXPECT_TRUE(service.RegisterCompiler(
    std::make_shared<CountingCompiler>(compile_calls)));

  // NOLINTNEXTLINE(*capturing-lambda-*)
  oxygen::co::Run(loop, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&ScriptCompilationService::ActivateAsync, &service);
      service.Run();

      ScriptCompilationService::Request request_a {
        .compile_key = ScriptCompilationService::CompileKey { 777 },
        .source = MakeSourceBlob({ 10, 11, 12 }),
      };
      ScriptCompilationService::Request request_b {
        .compile_key = ScriptCompilationService::CompileKey { 777 },
        .source = MakeSourceBlob({ 10, 11, 12 }),
      };

      auto first = service.CompileAsync(std::move(request_a));
      auto second = service.CompileAsync(std::move(request_b));
      const auto [a, b] = co_await AllOf(std::move(first), std::move(second));

      EXPECT_TRUE(a.success);
      EXPECT_TRUE(b.success);
      EXPECT_EQ(a.bytecode, b.bytecode);

      service.Stop();
      co_return oxygen::co::kJoin;
    };
  });

  EXPECT_EQ(compile_calls.load(), 1);
  EXPECT_EQ(service.InFlightCount(), 0);
}

NOLINT_TEST(ScriptCompilationServiceTest, CompletionSubscribersArePublishedOnce)
{
  TestEventLoop loop;
  ThreadPool pool(loop, 2);

  std::atomic<int> compile_calls = 0;
  std::atomic<int> subscriber_calls = 0;
  ScriptCompilationService service(observer_ptr { &pool });
  EXPECT_TRUE(service.RegisterCompiler(
    std::make_shared<CountingCompiler>(compile_calls)));

  // NOLINTNEXTLINE(*capturing-lambda-*)
  oxygen::co::Run(loop, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&ScriptCompilationService::ActivateAsync, &service);
      service.Run();

      constexpr ScriptCompilationService::CompileKey kCompileKey { 9001 };
      const auto subscription = service.Subscribe(
        kCompileKey, [&subscriber_calls](const ScriptCompileResult& result) {
          EXPECT_TRUE(result.success);
          ++subscriber_calls;
        });

      ScriptCompilationService::Request request {
        .compile_key = kCompileKey,
        .source = MakeSourceBlob({ 1, 2, 3 }),
      };

      const auto first
        = co_await service.CompileAsync(ScriptCompilationService::Request {
          .compile_key = request.compile_key,
          .source = MakeSourceBlob({ 1, 2, 3 }),
        });
      EXPECT_TRUE(first.success);
      service.OnFrameStart(oxygen::engine::internal::EngineTagFactory::Get());

      const auto second = co_await service.CompileAsync(std::move(request));
      EXPECT_TRUE(second.success);
      service.OnFrameStart(oxygen::engine::internal::EngineTagFactory::Get());
      EXPECT_FALSE(service.Unsubscribe(subscription));

      service.Stop();
      co_return oxygen::co::kJoin;
    };
  });

  EXPECT_EQ(compile_calls.load(), 1);
  EXPECT_EQ(subscriber_calls.load(), 1);
}

auto MakeTempCachePath() -> std::filesystem::path
{
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path()
    / ("oxygen_script_cache_" + std::to_string(now) + ".bin");
}

auto WriteCacheVersion(
  const std::filesystem::path& path, const uint32_t version) -> void
{
  std::fstream stream(path, std::ios::binary | std::ios::in | std::ios::out);
  ASSERT_TRUE(stream.is_open());
  constexpr std::streamoff kVersionOffset = 8;
  stream.seekp(kVersionOffset, std::ios::beg);
  stream.write(reinterpret_cast<const char*>(&version), sizeof(version));
  stream.flush();
}

auto CorruptCacheMagic(const std::filesystem::path& path) -> void
{
  std::fstream stream(path, std::ios::binary | std::ios::in | std::ios::out);
  ASSERT_TRUE(stream.is_open());
  constexpr char kCorruptByte = 'X';
  constexpr std::streamoff kMagicOffset = 0;
  stream.seekp(kMagicOffset, std::ios::beg);
  stream.write(&kCorruptByte, 1);
  stream.flush();
}

auto CompileAndStop(ScriptCompilationService& service, TestEventLoop& loop,
  const uint64_t key, std::vector<uint8_t> source) -> ScriptCompileResult
{
  ScriptCompileResult result;
  // NOLINTNEXTLINE(*capturing-lambda-*)
  oxygen::co::Run(loop, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&ScriptCompilationService::ActivateAsync, &service);
      service.Run();
      result = co_await service.CompileAsync(ScriptCompilationService::Request {
        .compile_key = ScriptCompilationService::CompileKey { key },
        .source = MakeSourceBlob(std::move(source)),
      });
      service.Stop();
      co_return oxygen::co::kJoin;
    };
  });
  return result;
}

auto CompileIntoPersistentCache(const std::filesystem::path& path,
  const uint64_t key, std::vector<uint8_t> source, std::atomic<int>& calls)
  -> ScriptCompileResult
{
  TestEventLoop loop;
  ScriptCompilationService service(observer_ptr<ThreadPool> {}, path);
  EXPECT_TRUE(service.RegisterCompiler(
    std::make_shared<SourceSizedCountingCompiler>(calls)));
  return CompileAndStop(service, loop, key, std::move(source));
}

auto CacheIndexOffset(const std::filesystem::path& path) -> uint64_t
{
  std::ifstream input(path, std::ios::binary);
  input.seekg(16);
  std::array<char, sizeof(uint64_t)> bytes {};
  input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  EXPECT_TRUE(input.good());
  return std::bit_cast<uint64_t>(bytes);
}

template <typename T>
auto OverwriteCacheField(const std::filesystem::path& path,
  const uint64_t offset, const T value) -> void
{
  std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
  ASSERT_TRUE(file.is_open());
  file.seekp(static_cast<std::streamoff>(offset));
  const auto bytes = std::bit_cast<std::array<char, sizeof(T)>>(value);
  file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  file.flush();
  ASSERT_TRUE(file.good());
}

#ifdef _WIN32
NOLINT_TEST(ScriptCompilationServiceTest,
  PersistentCacheFailedPublicationCanRetryWithoutDroppingPendingResults)
{
  for (const auto deferred : { false, true }) {
    const auto path = MakeTempCachePath();
    std::atomic<int> calls = 0;
    ASSERT_TRUE(CompileIntoPersistentCache(path, 100U, { 1 }, calls).success);
    const auto read_file = [&path]() -> std::vector<char> {
      std::ifstream input(path, std::ios::binary);
      return { std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>() };
    };
    const auto previous_file = read_file();
    ASSERT_FALSE(previous_file.empty());

    TestEventLoop loop;
    ScriptCompilationService service(observer_ptr<ThreadPool> {}, path);
    service.SetDeferredPersistence(deferred);
    EXPECT_TRUE(service.RegisterCompiler(
      std::make_shared<SourceSizedCountingCompiler>(calls)));

    // A reader that permits reads/writes but not deletion deterministically
    // prevents MoveFileEx replacement while leaving the old cache readable.
    auto blocker = CreateFileW(path.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(blocker, INVALID_HANDLE_VALUE);
    const auto close_blocker
      = oxygen::ScopeGuard([&blocker]() noexcept -> void {
          if (blocker != INVALID_HANDLE_VALUE) {
            CloseHandle(blocker);
          }
        });

    // NOLINTNEXTLINE(*capturing-lambda-*)
    oxygen::co::Run(loop, [&]() -> Co<> {
      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&ScriptCompilationService::ActivateAsync, &service);
        service.Run();
        for (const auto key : { 200U, 300U }) {
          const auto result
            = co_await service.CompileAsync(ScriptCompilationService::Request {
              .compile_key = ScriptCompilationService::CompileKey { key },
              .source = MakeSourceBlob({ static_cast<uint8_t>(key / 100U) }),
            });
          EXPECT_TRUE(result.success);
          service.FlushPersistentCache();
          EXPECT_EQ(read_file(), previous_file);
        }
        EXPECT_NE(CloseHandle(blocker), FALSE);
        blocker = INVALID_HANDLE_VALUE;
        service.FlushPersistentCache();
        service.Stop();
        co_return oxygen::co::kJoin;
      };
    });
    EXPECT_EQ(calls.load(), 3);

    // No compiler is registered after restart: both failed/newer pending
    // entries and the previously published entry must be served from disk.
    for (const auto key : { 100U, 200U, 300U }) {
      TestEventLoop replay_loop;
      ScriptCompilationService reader(observer_ptr<ThreadPool> {}, path);
      const auto result = CompileAndStop(reader, replay_loop, key, { 0 });
      ASSERT_TRUE(result.success);
      ASSERT_NE(result.bytecode, nullptr);
      EXPECT_TRUE(std::ranges::equal(result.bytecode->BytesView(),
        std::array<uint8_t, 2> { static_cast<uint8_t>(key / 100U), 0xEE }));
      EXPECT_EQ(reader.GetCounters().l2_hits, 1U);
      EXPECT_EQ(reader.GetCounters().compile_started, 0U);
    }
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  }
}
#endif

NOLINT_TEST(ScriptCompilationServiceTest,
  PersistentCacheRejectsPayloadAndChecksumCorruption)
{
  for (const auto corrupt_payload : { true, false }) {
    const auto path = MakeTempCachePath();
    std::atomic<int> calls = 0;
    ASSERT_TRUE(
      CompileIntoPersistentCache(path, 100U, { 1, 2, 3 }, calls).success);
    if (corrupt_payload) {
      OverwriteCacheField(path, 32U, uint8_t { 0x7F });
    } else {
      OverwriteCacheField(path, CacheIndexOffset(path) + 44U, uint64_t { 0 });
    }
    const auto result
      = CompileIntoPersistentCache(path, 100U, { 1, 2, 3 }, calls);
    ASSERT_TRUE(result.success);
    ASSERT_NE(result.bytecode, nullptr);
    EXPECT_TRUE(std::ranges::equal(
      result.bytecode->BytesView(), std::array<uint8_t, 4> { 1, 2, 3, 0xEE }));
    EXPECT_EQ(calls.load(), 2);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  }
}

NOLINT_TEST(ScriptCompilationServiceTest, PersistentCacheRejectsMalformedIndex)
{
  for (const auto malformed : { 0, 1, 2, 3, 4, 5, 6, 7 }) {
    const auto path = MakeTempCachePath();
    std::atomic<int> calls = 0;
    ASSERT_TRUE(
      CompileIntoPersistentCache(path, 100U, { 1, 2, 3 }, calls).success);
    const auto index = CacheIndexOffset(path);
    switch (malformed) {
    case 0: // Older cache formats are invalidated without an old-layout reader.
      WriteCacheVersion(path, 1U);
      break;
    case 1: // Index overlaps the fixed header.
      OverwriteCacheField(path, 16U, uint64_t { 0 });
      break;
    case 2: // Entry count cannot fit the file's index range.
      OverwriteCacheField(path, 24U, std::numeric_limits<uint32_t>::max());
      break;
    case 3: // Overflowing payload range.
      OverwriteCacheField(
        path, index + 8U, std::numeric_limits<uint64_t>::max());
      break;
    case 4: // Empty bytecode is never a valid cache entry.
      OverwriteCacheField(path, index + 16U, uint32_t { 0 });
      break;
    case 5:
      OverwriteCacheField(path, index + 20U, uint32_t { 99 });
      break;
    case 6:
      OverwriteCacheField(path, index + 24U, uint32_t { 99 });
      break;
    case 7:
      OverwriteCacheField(path, index + 28U, uint32_t { 99 });
      break;
    default:
      FAIL() << "Unhandled malformed cache test case";
    }
    const auto result
      = CompileIntoPersistentCache(path, 100U, { 1, 2, 3 }, calls);
    ASSERT_TRUE(result.success);
    EXPECT_EQ(calls.load(), 2);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  }
}

NOLINT_TEST(ScriptCompilationServiceTest,
  PersistentCacheReplacementInvalidatesStaleOffsetsBeforeVmDelivery)
{
  const auto path = MakeTempCachePath();
  std::atomic<int> writer_calls = 0;
  ASSERT_TRUE(
    CompileIntoPersistentCache(path, 30U, { 3, 4 }, writer_calls).success);

  // This service retains the first file's index while a second publisher adds
  // a lower key, shifting the original payload in the shared sorted file.
  TestEventLoop loop;
  ScriptCompilationService stale_reader(observer_ptr<ThreadPool> {}, path);
  std::atomic<int> reader_calls = 0;
  EXPECT_TRUE(stale_reader.RegisterCompiler(
    std::make_shared<SourceSizedCountingCompiler>(reader_calls)));
  ASSERT_TRUE(
    CompileIntoPersistentCache(path, 10U, { 9, 9, 9 }, writer_calls).success);

  const auto result = CompileAndStop(stale_reader, loop, 30U, { 3, 4 });
  ASSERT_TRUE(result.success);
  ASSERT_NE(result.bytecode, nullptr);
  EXPECT_TRUE(std::ranges::equal(
    result.bytecode->BytesView(), std::array<uint8_t, 3> { 3, 4, 0xEE }));
  EXPECT_EQ(reader_calls.load(), 1);
  EXPECT_EQ(stale_reader.GetCounters().l2_hits, 0U);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

NOLINT_TEST(ScriptCompilationServiceTest,
  PersistentCacheReplacementCannotCopyStalePayloadIntoNextSnapshot)
{
  const auto path = MakeTempCachePath();
  std::atomic<int> writer_calls = 0;
  ASSERT_TRUE(
    CompileIntoPersistentCache(path, 30U, { 3, 4 }, writer_calls).success);
  TestEventLoop loop;
  ScriptCompilationService stale_reader(observer_ptr<ThreadPool> {}, path);
  std::atomic<int> reader_calls = 0;
  EXPECT_TRUE(stale_reader.RegisterCompiler(
    std::make_shared<SourceSizedCountingCompiler>(reader_calls)));
  ASSERT_TRUE(
    CompileIntoPersistentCache(path, 10U, { 9, 9, 9 }, writer_calls).success);
  ASSERT_TRUE(CompileAndStop(stale_reader, loop, 40U, { 4, 5 }).success);

  // The stale reader's flush must skip key30, rather than checksum and publish
  // the unrelated bytes now found at key30's old offset.
  std::atomic<int> recovery_calls = 0;
  const auto result
    = CompileIntoPersistentCache(path, 30U, { 3, 4 }, recovery_calls);
  ASSERT_TRUE(result.success);
  ASSERT_NE(result.bytecode, nullptr);
  EXPECT_TRUE(std::ranges::equal(
    result.bytecode->BytesView(), std::array<uint8_t, 3> { 3, 4, 0xEE }));
  EXPECT_EQ(recovery_calls.load(), 1);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

NOLINT_TEST(ScriptCompilationServiceTest, SequentialSameKeyHitsL1Cache)
{
  TestEventLoop loop;
  ThreadPool pool(loop, 2);

  std::atomic<int> compile_calls = 0;
  ScriptCompilationService service(observer_ptr { &pool });
  EXPECT_TRUE(service.RegisterCompiler(
    std::make_shared<CountingCompiler>(compile_calls)));

  // NOLINTNEXTLINE(*capturing-lambda-*)
  oxygen::co::Run(loop, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&ScriptCompilationService::ActivateAsync, &service);
      service.Run();

      ScriptCompilationService::Request first {
        .compile_key = ScriptCompilationService::CompileKey { 31337 },
        .source = MakeSourceBlob({ 1, 3, 3, 7 }),
      };
      ScriptCompilationService::Request second {
        .compile_key = ScriptCompilationService::CompileKey { 31337 },
        .source = MakeSourceBlob({ 1, 3, 3, 7 }),
      };

      const auto first_result = co_await service.CompileAsync(std::move(first));
      EXPECT_TRUE(first_result.success);
      const auto second_result
        = co_await service.CompileAsync(std::move(second));
      EXPECT_TRUE(second_result.success);
      EXPECT_NE(first_result.bytecode, nullptr);
      EXPECT_NE(second_result.bytecode, nullptr);
      if (first_result.bytecode != nullptr
        && second_result.bytecode != nullptr) {
        EXPECT_TRUE(std::ranges::equal(first_result.bytecode->BytesView(),
          second_result.bytecode->BytesView()));
      }

      service.Stop();
      co_return oxygen::co::kJoin;
    };
  });

  EXPECT_EQ(compile_calls.load(), 1);
}

NOLINT_TEST(ScriptCompilationServiceTest, L1CacheEvictsOldestEntries)
{
  TestEventLoop loop;
  ThreadPool pool(loop, 2);

  std::atomic<int> compile_calls = 0;
  ScriptCompilationService service(observer_ptr { &pool });
  EXPECT_TRUE(service.RegisterCompiler(
    std::make_shared<SourceSizedCountingCompiler>(compile_calls)));

  // NOLINTNEXTLINE(*capturing-lambda-*)
  oxygen::co::Run(loop, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&ScriptCompilationService::ActivateAsync, &service);
      service.Run();

      constexpr uint64_t kFirstKey = 1;
      constexpr uint64_t kBeyondL1Capacity = 300;
      const std::vector<uint8_t> large_payload(512 * 1024, 0x42); // NOLINT

      for (uint64_t key = kFirstKey; key <= kBeyondL1Capacity; ++key) {
        const auto result
          = co_await service.CompileAsync(ScriptCompilationService::Request {
            .compile_key = ScriptCompilationService::CompileKey { key },
            .source = MakeSourceBlob(large_payload),
          });
        EXPECT_TRUE(result.success);
      }

      const auto evicted_result
        = co_await service.CompileAsync(ScriptCompilationService::Request {
          .compile_key = ScriptCompilationService::CompileKey { kFirstKey },
          .source = MakeSourceBlob(large_payload),
        });
      EXPECT_TRUE(evicted_result.success);

      service.Stop();
      co_return oxygen::co::kJoin;
    };
  });

  EXPECT_EQ(compile_calls.load(), 301);
}

NOLINT_TEST(ScriptCompilationServiceTest, L1CacheKeepsRecentEntries)
{
  TestEventLoop loop;
  ThreadPool pool(loop, 2);

  std::atomic<int> compile_calls = 0;
  ScriptCompilationService service(observer_ptr { &pool });
  EXPECT_TRUE(service.RegisterCompiler(
    std::make_shared<SourceSizedCountingCompiler>(compile_calls)));

  // NOLINTNEXTLINE(*capturing-lambda-*)
  oxygen::co::Run(loop, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&ScriptCompilationService::ActivateAsync, &service);
      service.Run();

      constexpr uint64_t kBeyondL1Capacity = 300;
      const std::vector<uint8_t> large_payload(512 * 1024, 0x42); // NOLINT

      for (uint64_t key = 1; key <= kBeyondL1Capacity; ++key) {
        const auto result
          = co_await service.CompileAsync(ScriptCompilationService::Request {
            .compile_key = ScriptCompilationService::CompileKey { key },
            .source = MakeSourceBlob(large_payload),
          });
        EXPECT_TRUE(result.success);
      }

      const auto recent_result
        = co_await service.CompileAsync(ScriptCompilationService::Request {
          .compile_key
          = ScriptCompilationService::CompileKey { kBeyondL1Capacity },
          .source = MakeSourceBlob(large_payload),
        });
      EXPECT_TRUE(recent_result.success);

      service.Stop();
      co_return oxygen::co::kJoin;
    };
  });

  EXPECT_EQ(compile_calls.load(), 300);
}

NOLINT_TEST(ScriptCompilationServiceTest, PersistentCacheRoundtripAcrossRestart)
{
  const auto cache_path = MakeTempCachePath();
  std::atomic<int> compile_calls = 0;

  {
    TestEventLoop loop;
    ThreadPool pool(loop, 2);
    ScriptCompilationService service(observer_ptr { &pool }, cache_path);
    EXPECT_TRUE(service.RegisterCompiler(
      std::make_shared<ImmediateCountingCompiler>(compile_calls)));

    // NOLINTNEXTLINE(*capturing-lambda-*)
    oxygen::co::Run(loop, [&]() -> Co<> {
      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&ScriptCompilationService::ActivateAsync, &service);
        service.Run();

        const auto result
          = co_await service.CompileAsync(ScriptCompilationService::Request {
            .compile_key = ScriptCompilationService::CompileKey { 999 },
            .source = MakeSourceBlob({ 1, 2, 3 }),
          });
        EXPECT_TRUE(result.success);
        EXPECT_NE(result.bytecode, nullptr);
        if (result.bytecode != nullptr) {
          EXPECT_FALSE(result.bytecode->IsEmpty());
        }

        service.Stop();
        co_return oxygen::co::kJoin;
      };
    });
  }

  {
    TestEventLoop loop;
    ScriptCompilationService service(observer_ptr<ThreadPool> {}, cache_path);

    // NOLINTNEXTLINE(*capturing-lambda-*)
    oxygen::co::Run(loop, [&]() -> Co<> {
      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&ScriptCompilationService::ActivateAsync, &service);
        service.Run();

        const auto result
          = co_await service.CompileAsync(ScriptCompilationService::Request {
            .compile_key = ScriptCompilationService::CompileKey { 999 },
            .source = MakeSourceBlob({ 0 }),
          });
        EXPECT_TRUE(result.success);
        EXPECT_NE(result.bytecode, nullptr);
        if (result.bytecode != nullptr) {
          EXPECT_FALSE(result.bytecode->IsEmpty());
        }

        service.Stop();
        co_return oxygen::co::kJoin;
      };
    });
  }

  EXPECT_EQ(compile_calls.load(), 1);
  std::error_code ec {};
  std::filesystem::remove(cache_path, ec);
}

NOLINT_TEST(
  ScriptCompilationServiceTest, PersistentCacheVersionMismatchInvalidates)
{
  const auto cache_path = MakeTempCachePath();
  std::atomic<int> compile_calls = 0;

  {
    TestEventLoop loop;
    ThreadPool pool(loop, 2);
    ScriptCompilationService service(observer_ptr { &pool }, cache_path);
    EXPECT_TRUE(service.RegisterCompiler(
      std::make_shared<ImmediateCountingCompiler>(compile_calls)));

    // NOLINTNEXTLINE(*capturing-lambda-*)
    oxygen::co::Run(loop, [&]() -> Co<> {
      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&ScriptCompilationService::ActivateAsync, &service);
        service.Run();

        const auto result
          = co_await service.CompileAsync(ScriptCompilationService::Request {
            .compile_key = ScriptCompilationService::CompileKey { 1111 },
            .source = MakeSourceBlob({ 1 }),
          });
        EXPECT_TRUE(result.success);

        service.Stop();
        co_return oxygen::co::kJoin;
      };
    });
  }

  WriteCacheVersion(cache_path, 999U);

  {
    TestEventLoop loop;
    ScriptCompilationService service(observer_ptr<ThreadPool> {}, cache_path);

    // NOLINTNEXTLINE(*capturing-lambda-*)
    oxygen::co::Run(loop, [&]() -> Co<> {
      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&ScriptCompilationService::ActivateAsync, &service);
        service.Run();

        const auto result
          = co_await service.CompileAsync(ScriptCompilationService::Request {
            .compile_key = ScriptCompilationService::CompileKey { 1111 },
            .source = MakeSourceBlob({ 0 }),
          });
        EXPECT_FALSE(result.success);
        EXPECT_FALSE(result.diagnostics.empty());

        service.Stop();
        co_return oxygen::co::kJoin;
      };
    });
  }

  EXPECT_EQ(compile_calls.load(), 1);
  std::error_code ec {};
  std::filesystem::remove(cache_path, ec);
}

NOLINT_TEST(
  ScriptCompilationServiceTest, PersistentCacheCorruptionFallsBackToCompile)
{
  const auto cache_path = MakeTempCachePath();
  std::atomic<int> compile_calls = 0;

  {
    TestEventLoop loop;
    ThreadPool pool(loop, 2);
    ScriptCompilationService service(observer_ptr { &pool }, cache_path);
    EXPECT_TRUE(service.RegisterCompiler(
      std::make_shared<ImmediateCountingCompiler>(compile_calls)));

    // NOLINTNEXTLINE(*capturing-lambda-*)
    oxygen::co::Run(loop, [&]() -> Co<> {
      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&ScriptCompilationService::ActivateAsync, &service);
        service.Run();

        const auto result
          = co_await service.CompileAsync(ScriptCompilationService::Request {
            .compile_key = ScriptCompilationService::CompileKey { 2222 },
            .source = MakeSourceBlob({ 1 }),
          });
        EXPECT_TRUE(result.success);

        service.Stop();
        co_return oxygen::co::kJoin;
      };
    });
  }

  CorruptCacheMagic(cache_path);

  {
    TestEventLoop loop;
    ThreadPool pool(loop, 2);
    ScriptCompilationService service(observer_ptr { &pool }, cache_path);
    EXPECT_TRUE(service.RegisterCompiler(
      std::make_shared<ImmediateCountingCompiler>(compile_calls)));

    // NOLINTNEXTLINE(*capturing-lambda-*)
    oxygen::co::Run(loop, [&]() -> Co<> {
      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&ScriptCompilationService::ActivateAsync, &service);
        service.Run();

        const auto result
          = co_await service.CompileAsync(ScriptCompilationService::Request {
            .compile_key = ScriptCompilationService::CompileKey { 2222 },
            .source = MakeSourceBlob({ 0 }),
          });
        EXPECT_TRUE(result.success);

        service.Stop();
        co_return oxygen::co::kJoin;
      };
    });
  }

  EXPECT_EQ(compile_calls.load(), 2);
  std::error_code ec {};
  std::filesystem::remove(cache_path, ec);
}

NOLINT_TEST(ScriptCompilationServiceTest, UnsubscribePreventsCompletionCallback)
{
  TestEventLoop loop;
  ThreadPool pool(loop, 2);

  std::atomic<int> compile_calls = 0;
  std::atomic<int> subscriber_calls = 0;
  ScriptCompilationService service(observer_ptr { &pool });
  EXPECT_TRUE(service.RegisterCompiler(
    std::make_shared<CountingCompiler>(compile_calls)));

  // NOLINTNEXTLINE(*capturing-lambda-*)
  oxygen::co::Run(loop, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&ScriptCompilationService::ActivateAsync, &service);
      service.Run();

      constexpr ScriptCompilationService::CompileKey kCompileKey { 42 };
      const auto subscription = service.Subscribe(
        kCompileKey, [&subscriber_calls](const ScriptCompileResult&) {
          ++subscriber_calls;
        });
      EXPECT_TRUE(service.Unsubscribe(subscription));

      ScriptCompilationService::Request request {
        .compile_key = kCompileKey,
        .source = MakeSourceBlob({ 9, 9, 9 }),
      };

      const auto result = co_await service.CompileAsync(std::move(request));
      EXPECT_TRUE(result.success);
      service.OnFrameStart(oxygen::engine::internal::EngineTagFactory::Get());

      service.Stop();
      co_return oxygen::co::kJoin;
    };
  });

  EXPECT_EQ(compile_calls.load(), 1);
  EXPECT_EQ(subscriber_calls.load(), 0);
}

NOLINT_TEST(ScriptCompilationServiceTest, CountersTrackCompileAndL1Hit)
{
  TestEventLoop loop;
  ThreadPool pool(loop, 2);

  std::atomic<int> compile_calls = 0;
  ScriptCompilationService service(observer_ptr { &pool });
  EXPECT_TRUE(service.RegisterCompiler(
    std::make_shared<ImmediateCountingCompiler>(compile_calls)));

  // NOLINTNEXTLINE(*capturing-lambda-*)
  oxygen::co::Run(loop, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&ScriptCompilationService::ActivateAsync, &service);
      service.Run();

      const auto first
        = co_await service.CompileAsync(ScriptCompilationService::Request {
          .compile_key = ScriptCompilationService::CompileKey { 1001 },
          .source = MakeSourceBlob({ 1, 2, 3 }),
        });
      EXPECT_TRUE(first.success);

      const auto second
        = co_await service.CompileAsync(ScriptCompilationService::Request {
          .compile_key = ScriptCompilationService::CompileKey { 1001 },
          .source = MakeSourceBlob({ 1, 2, 3 }),
        });
      EXPECT_TRUE(second.success);

      service.Stop();
      co_return oxygen::co::kJoin;
    };
  });

  const auto counters = service.GetCounters();
  EXPECT_EQ(counters.compile_started, 1U);
  EXPECT_EQ(counters.compile_succeeded, 1U);
  EXPECT_EQ(counters.compile_failed, 0U);
  EXPECT_EQ(counters.l1_hits, 1U);
  EXPECT_EQ(counters.l2_hits, 0U);
  EXPECT_EQ(counters.compile_latency_samples, 1U);
  EXPECT_GE(counters.compile_latency_total_us, counters.compile_latency_max_us);
}

NOLINT_TEST(ScriptCompilationServiceTest, CountersTrackL2Hit)
{
  const auto cache_path = MakeTempCachePath();
  std::atomic<int> compile_calls = 0;

  {
    TestEventLoop loop;
    ThreadPool pool(loop, 2);
    ScriptCompilationService service(observer_ptr { &pool }, cache_path);
    EXPECT_TRUE(service.RegisterCompiler(
      std::make_shared<ImmediateCountingCompiler>(compile_calls)));

    // NOLINTNEXTLINE(*capturing-lambda-*)
    oxygen::co::Run(loop, [&]() -> Co<> {
      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&ScriptCompilationService::ActivateAsync, &service);
        service.Run();
        const auto result
          = co_await service.CompileAsync(ScriptCompilationService::Request {
            .compile_key = ScriptCompilationService::CompileKey { 3003 },
            .source = MakeSourceBlob({ 7, 8, 9 }),
          });
        EXPECT_TRUE(result.success);
        service.Stop();
        co_return oxygen::co::kJoin;
      };
    });
  }

  {
    TestEventLoop loop;
    ScriptCompilationService service(observer_ptr<ThreadPool> {}, cache_path);

    // NOLINTNEXTLINE(*capturing-lambda-*)
    oxygen::co::Run(loop, [&]() -> Co<> {
      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&ScriptCompilationService::ActivateAsync, &service);
        service.Run();
        const auto result
          = co_await service.CompileAsync(ScriptCompilationService::Request {
            .compile_key = ScriptCompilationService::CompileKey { 3003 },
            .source = MakeSourceBlob({ 0 }),
          });
        EXPECT_TRUE(result.success);
        service.Stop();
        co_return oxygen::co::kJoin;
      };
    });

    const auto counters = service.GetCounters();
    EXPECT_EQ(counters.l2_hits, 1U);
    EXPECT_EQ(counters.l1_hits, 0U);
    EXPECT_EQ(counters.compile_started, 0U);
    EXPECT_EQ(counters.compile_succeeded, 0U);
    EXPECT_EQ(counters.compile_failed, 0U);
  }

  std::error_code ec {};
  std::filesystem::remove(cache_path, ec);
}

NOLINT_TEST(ScriptCompilationServiceTest,
  AcquireForSlotReturnsPlaceholderAndPublishesReady)
{
  TestEventLoop loop;
  ThreadPool pool(loop, 2);

  std::atomic<int> compile_calls = 0;
  std::atomic<int> ready_calls = 0;
  std::atomic<int> failed_calls = 0;
  std::shared_ptr<const ScriptBytecodeBlob> ready_bytecode;
  ScriptCompilationService service(observer_ptr { &pool });
  EXPECT_TRUE(service.RegisterCompiler(
    std::make_shared<CountingCompiler>(compile_calls)));

  // NOLINTNEXTLINE(*capturing-lambda-*)
  oxygen::co::Run(loop, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&ScriptCompilationService::ActivateAsync, &service);
      service.Run();

      ScriptCompilationService::Request request {
        .compile_key = ScriptCompilationService::CompileKey { 12345 },
        .source = MakeSourceBlob({ 7, 8, 9 }),
      };
      oxygen::co::Event completion;

      auto acquire = service.AcquireForSlot(std::move(request),
        ScriptCompilationService::SlotAcquireCallbacks {
          .on_ready =
            [&ready_calls, &ready_bytecode, &completion](
              std::shared_ptr<const ScriptBytecodeBlob> bytecode) {
              ++ready_calls;
              ready_bytecode = std::move(bytecode);
              completion.Trigger();
            },
          .on_failed =
            [&failed_calls, &completion](const std::string& /*diagnostic*/) {
              ++failed_calls;
              completion.Trigger();
            },
        });

      EXPECT_EQ(acquire.placeholder, nullptr);
      n.Start([&]() -> Co<> {
        while (!completion.Triggered()) {
          service.OnFrameStart(
            oxygen::engine::internal::EngineTagFactory::Get());
          co_await pool.Run(
            [](ThreadPool::CancelToken) { std::this_thread::sleep_for(1ms); });
        }
        co_return;
      });
      co_await completion;
      service.OnFrameStart(oxygen::engine::internal::EngineTagFactory::Get());

      service.Stop();
      co_return oxygen::co::kJoin;
    };
  });

  EXPECT_EQ(compile_calls.load(), 1);
  EXPECT_EQ(ready_calls.load(), 1);
  EXPECT_EQ(failed_calls.load(), 0);
  ASSERT_NE(ready_bytecode, nullptr);
  EXPECT_FALSE(ready_bytecode->IsEmpty());
}

NOLINT_TEST(ScriptCompilationServiceTest, AcquireForSlotPublishesFailure)
{
  TestEventLoop loop;
  ScriptCompilationService service(observer_ptr<ThreadPool> {});

  std::atomic<int> ready_calls = 0;
  std::atomic<int> failed_calls = 0;

  // NOLINTNEXTLINE(*capturing-lambda-*)
  oxygen::co::Run(loop, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&ScriptCompilationService::ActivateAsync, &service);
      service.Run();

      ScriptCompilationService::Request request {
        .compile_key = ScriptCompilationService::CompileKey { 54321 },
        .source = MakeSourceBlob({ 1, 2, 3 }),
      };
      oxygen::co::Event completion;

      auto acquire = service.AcquireForSlot(std::move(request),
        ScriptCompilationService::SlotAcquireCallbacks {
          .on_ready =
            [&ready_calls, &completion](
              std::shared_ptr<const ScriptBytecodeBlob>) {
              ++ready_calls;
              completion.Trigger();
            },
          .on_failed =
            [&failed_calls, &completion](const std::string& diagnostic) {
              EXPECT_FALSE(diagnostic.empty());
              ++failed_calls;
              completion.Trigger();
            },
        });

      EXPECT_EQ(acquire.placeholder, nullptr);
      n.Start([&]() -> Co<> {
        while (!completion.Triggered()) {
          service.OnFrameStart(
            oxygen::engine::internal::EngineTagFactory::Get());
          co_await loop.Sleep(1ms);
        }
        co_return;
      });
      co_await completion;
      service.OnFrameStart(oxygen::engine::internal::EngineTagFactory::Get());

      service.Stop();
      co_return oxygen::co::kJoin;
    };
  });

  EXPECT_EQ(ready_calls.load(), 0);
  EXPECT_EQ(failed_calls.load(), 1);
}

// NOLINTEND(*-magic-numbers)

} // namespace
