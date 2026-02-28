//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <thread>

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
