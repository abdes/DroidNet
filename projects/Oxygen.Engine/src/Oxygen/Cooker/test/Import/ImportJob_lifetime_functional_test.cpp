//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/ImportJob.cpp

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Object.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/ImportJob.h>
#include <Oxygen/Cooker/Import/Internal/ImportJobParams.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedIndexRegistry.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableRegistry.h>
#include <Oxygen/Cooker/test/Support/HoldingStorage.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace {
namespace co = oxygen::co;
namespace imp = oxygen::content::import;

using oxygen::cooker::testing::HoldingStorage;

struct ProducerState final {
  bool exited = false;
  bool destroyed_after_exit = false;
  co::Event destroyed;
};

class LifetimeProducer final : public oxygen::Object {
  OXYGEN_TYPED(LifetimeProducer)
public:
  explicit LifetimeProducer(ProducerState& state)
    : state_(state)
  {
  }
  ~LifetimeProducer() override
  {
    state_.destroyed_after_exit = state_.exited;
    state_.destroyed.Trigger();
  }
  OXYGEN_MAKE_NON_COPYABLE(LifetimeProducer)
  OXYGEN_MAKE_NON_MOVABLE(LifetimeProducer)
  auto Start(co::Nursery& nursery) -> void
  {
    nursery.Start(&LifetimeProducer::Run, this);
  }

private:
  auto Exit() -> co::Co<>
  {
    state_.exited = true;
    co_return;
  }
  auto Run() -> co::Co<>
  {
    co_await co::UntilCancelledAnd([this] -> co::Co<> { return Exit(); });
  }
  ProducerState& state_;
};

class EmittingJob final : public imp::detail::ImportJob {
public:
  OXYGEN_TYPED(EmittingJob)
public:
  EmittingJob(imp::detail::ImportJobParams params, ProducerState& state)
    : ImportJob(std::move(params))
    , state_(state)
  {
  }
  ~EmittingJob() override = default;
  OXYGEN_MAKE_NON_COPYABLE(EmittingJob)
  OXYGEN_MAKE_NON_MOVABLE(EmittingJob)
private:
  auto ExecuteAsync() -> co::Co<imp::ImportReport> override
  {
    EnsureCookedRoot();
    auto& session = Session();
    static_cast<void>(CreatePipeline<LifetimeProducer>(state_));
    const auto descriptor = oxygen::cooker::testing::MaterialBuilder {};
    session.AssetEmitter().Emit(
      oxygen::data::AssetKey::FromVirtualPath("/Content/test.omat"),
      oxygen::data::AssetType::kMaterial, "/Content/test.omat", "test.omat",
      descriptor.Bytes(), descriptor.References());
    co_return co_await session.Finalize();
  }
  ProducerState& state_;
};

constexpr auto kRecordedIo = std::chrono::microseconds { 17 };

class FinalizedFailureJob final : public imp::detail::ImportJob {
  OXYGEN_TYPED(FinalizedFailureJob)
public:
  using ImportJob::ImportJob;
  ~FinalizedFailureJob() override = default;
  OXYGEN_MAKE_NON_COPYABLE(FinalizedFailureJob)
  OXYGEN_MAKE_NON_MOVABLE(FinalizedFailureJob)

private:
  auto ExecuteAsync() -> co::Co<imp::ImportReport> override
  {
    EnsureCookedRoot();
    auto& session = Session();
    session.AddIoDuration(kRecordedIo);
    auto report = co_await session.Finalize();
    if (report.success) {
      throw std::runtime_error("Failure after recorded session work");
    }
    co_return report;
  }
};

auto RunFailingJob(imp::detail::ImportJob* job) -> co::Co<>
{
  OXCO_WITH_NURSERY(jobs)
  {
    co_await jobs.Start(&imp::detail::ImportJob::ActivateAsync, job);
    job->Run();
    co_await job->Wait();
    co_return co::kJoin;
  };
}

NOLINT_TEST(ImportJobLifetimeTest, ExceptionPreservesDrainedSessionTelemetry)
{
  const oxygen::testing::ScopedTempDir temp;
  imp::ImportEventLoop loop;
  auto reader = imp::CreateAsyncFileReader(loop.Driver());
  co::ThreadPool pool(loop, 1);
  imp::ResourceTableRegistry tables(loop.Driver(),
    oxygen::make_observer(&oxygen::content::cooked::RealCookerStorage()));
  imp::LooseCookedIndexRegistry indexes(
    oxygen::make_observer(&oxygen::content::cooked::RealCookerStorage()));
  const auto root = temp.Path() / "root";
  imp::ImportRequest request;
  request.source_path = root / "source.json";
  request.asset_virtual_path = "/.cooked/source.omat";
  request.source_mount_path
    = oxygen::cooker::test::SourceMountPath(request.source_path);
  request.cooked_root = root;
  imp::ImportReport result;
  auto job = FinalizedFailureJob(imp::detail::ImportJobParams {
    .id = imp::ImportJobId { 1 },
    .request = request,
    .on_complete = [&](auto, const auto& report) -> auto { result = report; },
    .on_progress = {},
    .cancel_event = {},
    .reader = oxygen::make_observer(reader.get()),
    .driver = oxygen::make_observer(&loop.Driver()),
    .thread_pool = oxygen::make_observer(&pool),
    .registry = oxygen::make_observer(&tables),
    .index_registry = oxygen::make_observer(&indexes),
    .concurrency = {},
    .script_compile_callback = {},
    .stop_token = {},
    .model_import_record = {},
  });
  co::Run(loop, RunFailingJob(&job));
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.telemetry.io_duration, kRecordedIo);
  EXPECT_TRUE(result.telemetry.source_load_duration.has_value());
  EXPECT_TRUE(result.telemetry.decode_duration.has_value());
  EXPECT_TRUE(result.telemetry.load_duration.has_value());
  EXPECT_TRUE(result.telemetry.cook_duration.has_value());
  EXPECT_TRUE(result.telemetry.emit_duration.has_value());
  EXPECT_TRUE(result.telemetry.finalize_duration.has_value());
  EXPECT_TRUE(result.telemetry.total_duration.has_value());
  EXPECT_GE(
    result.telemetry.total_duration, result.telemetry.finalize_duration);
}

NOLINT_TEST(ImportJobLifetimeTest,
  CancellationJoinsInFlightWritesBeforeRetiringAndAllowsSameRootRetry)
{
  const oxygen::testing::ScopedTempDir temp;
  imp::ImportEventLoop loop;
  auto reader = imp::CreateAsyncFileReader(loop.Driver());
  HoldingStorage storage(oxygen::content::cooked::RealCookerStorage());
  co::ThreadPool pool(loop, 1);
  const auto held_storage
    = oxygen::make_observer<oxygen::content::cooked::ImportStorage>(&storage);
  imp::ResourceTableRegistry tables(loop.Driver(), held_storage);
  imp::LooseCookedIndexRegistry indexes(held_storage);
  const auto root = temp.Path() / "oxygen_job_lifetime_retry";
  imp::ImportRequest request;
  request.source_path = root / "source.json";
  request.asset_virtual_path = "/.cooked/source.omat";
  request.source_mount_path
    = oxygen::cooker::test::SourceMountPath(request.source_path);
  request.cooked_root = root;
  unsigned completions = 0;
  imp::ImportReport first_report;
  imp::ImportReport retry_report;
  ProducerState first_state;
  ProducerState retry_state;
  const auto params = [&](imp::ImportCompletionCallback completion)
    -> imp::detail::ImportJobParams {
    return imp::detail::ImportJobParams {
      .id = imp::ImportJobId { 1 },
      .request = request,
      .on_complete = std::move(completion),
      .on_progress = {},
      .cancel_event = {},
      .reader = oxygen::make_observer(reader.get()),
      .driver = oxygen::make_observer(&loop.Driver()),
      .thread_pool = oxygen::make_observer(&pool),
      .registry = oxygen::make_observer(&tables),
      .index_registry = oxygen::make_observer(&indexes),
      .concurrency = {},
      .script_compile_callback = {},
      .stop_token = {},
      .model_import_record = {},
    };
  };
  EmittingJob first(params([&](auto, const auto& report) -> auto {
    ++completions;
    first_report = report;
  }),
    first_state);
  EmittingJob retry(params([&](auto, const auto& report) -> auto {
    ++completions;
    retry_report = report;
  }),
    retry_state);
  co::Run(loop, [&] -> co::Co<> {
    OXCO_WITH_NURSERY(jobs)
    {
      co_await jobs.Start(&EmittingJob::ActivateAsync, &first);
      first.Run();
      co_await storage.publication_held;
      EXPECT_EQ(storage.InFlight(), 1U);
      first.Stop();
      // The write in flight is not cancelled yet: the producers' nursery, and
      // so the job, wait for it before anything is retired.
      EXPECT_EQ(completions, 0U);
      EXPECT_EQ(storage.InFlight(), 1U);
      EXPECT_FALSE(first_state.destroyed.Triggered());
      EXPECT_FALSE(std::filesystem::exists(root / "container.index.bin"));
      storage.Release();
      co_await first.Wait();
      EXPECT_EQ(completions, 1U);
      EXPECT_FALSE(first_report.success);
      EXPECT_EQ(storage.InFlight(), 0U);
      EXPECT_TRUE(first_state.destroyed_after_exit);
      co_await jobs.Start(&EmittingJob::ActivateAsync, &retry);
      retry.Run();
      co_await retry.Wait();
      co_return co::kJoin;
    };
  });
  EXPECT_EQ(completions, 2U);
  EXPECT_TRUE(retry_report.success);
  EXPECT_TRUE(retry_report.packaging.index_written);
  EXPECT_TRUE(retry_state.destroyed_after_exit);
  const auto index = oxygen::content::lc::LooseCookedIndex::LoadFromRoot(root);
  EXPECT_NO_THROW(
    index.ValidateContent(root, oxygen::content::lc::IntegrityCheck::kFull));
}

} // namespace
