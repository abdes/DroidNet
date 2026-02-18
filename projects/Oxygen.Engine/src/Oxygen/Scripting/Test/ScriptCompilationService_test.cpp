//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
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
auto EngineTagFactory::Get() noexcept -> EngineTag { return EngineTag {}; }
} // namespace oxygen::engine::internal

namespace {

using oxygen::observer_ptr;
using oxygen::co::AllOf;
using oxygen::co::Co;
using oxygen::co::ThreadPool;
using oxygen::co::testing::TestEventLoop;
using oxygen::data::pak::ScriptLanguage;
using oxygen::scripting::CompileMode;
using oxygen::scripting::IScriptCompiler;
using oxygen::scripting::ScriptCompilationService;
using oxygen::scripting::ScriptCompileResult;

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

  [[nodiscard]] auto Compile(std::span<const uint8_t> source,
    const CompileMode /*mode*/) const -> ScriptCompileResult override
  {
    (void)source;
    ++compile_calls_;
    ScriptCompileResult result {};
    result.success = true;
    result.bytecode = { 0xCA, 0xFE, 0xBA, 0xBE };
    std::this_thread::sleep_for(2ms);
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
        .language = ScriptLanguage::kLuau,
        .source = { 1, 2, 3, 4 },
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

      ScriptCompilationService::Request request {
        .compile_key = ScriptCompilationService::CompileKey { 777 },
        .language = ScriptLanguage::kLuau,
        .source = { 10, 11, 12 },
      };

      auto first = service.CompileAsync(request);
      auto second = service.CompileAsync(request);
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
        .language = ScriptLanguage::kLuau,
        .source = { 1, 2, 3 },
      };

      const auto first = co_await service.CompileAsync(request);
      EXPECT_TRUE(first.success);
      service.OnFrameStart(oxygen::engine::internal::EngineTagFactory::Get());

      const auto second = co_await service.CompileAsync(request);
      EXPECT_TRUE(second.success);
      service.OnFrameStart(oxygen::engine::internal::EngineTagFactory::Get());
      EXPECT_FALSE(service.Unsubscribe(subscription));

      service.Stop();
      co_return oxygen::co::kJoin;
    };
  });

  EXPECT_EQ(compile_calls.load(), 2);
  EXPECT_EQ(subscriber_calls.load(), 1);
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
        .language = ScriptLanguage::kLuau,
        .source = { 9, 9, 9 },
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

NOLINT_TEST(ScriptCompilationServiceTest,
  AcquireForSlotReturnsPlaceholderAndPublishesReady)
{
  TestEventLoop loop;
  ThreadPool pool(loop, 2);

  std::atomic<int> compile_calls = 0;
  std::atomic<int> ready_calls = 0;
  std::atomic<int> failed_calls = 0;
  std::vector<uint8_t> ready_bytecode;
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
        .language = ScriptLanguage::kLuau,
        .source = { 7, 8, 9 },
      };
      oxygen::co::Event completion;

      auto acquire = service.AcquireForSlot(std::move(request),
        ScriptCompilationService::SlotAcquireCallbacks {
          .on_ready =
            [&ready_calls, &ready_bytecode, &completion](
              std::vector<uint8_t> bytecode) {
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
  EXPECT_FALSE(ready_bytecode.empty());
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
        .language = ScriptLanguage::kLuau,
        .source = { 1, 2, 3 },
      };
      oxygen::co::Event completion;

      auto acquire = service.AcquireForSlot(std::move(request),
        ScriptCompilationService::SlotAcquireCallbacks {
          .on_ready =
            [&ready_calls, &completion](std::vector<uint8_t>) {
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
