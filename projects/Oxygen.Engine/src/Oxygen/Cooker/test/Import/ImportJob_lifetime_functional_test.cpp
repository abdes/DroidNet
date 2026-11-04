//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/ImportJob.cpp

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Object.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportProgress.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/ImportJob.h>
#include <Oxygen/Cooker/Import/Internal/ImportJobParams.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedIndexRegistry.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableRegistry.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Cooker/test/Support/HoldingStorage.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Awaitables.h>
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
  request.asset_virtual_path = "/Cooked/source.omat";
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
  request.asset_virtual_path = "/Cooked/source.omat";
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

constexpr auto kGatheredCode = std::string_view { "test.gathered" };

[[nodiscard]] auto MakeGatheredWarning() -> imp::ImportDiagnostic
{
  return imp::ImportDiagnostic {
    .severity = imp::ImportSeverity::kWarning,
    .code = std::string(kGatheredCode),
    .message = "Found before the job was cut short",
    .source_path = {},
    .object_path = {},
  };
}

//! Gathers a diagnostic, then waits to be stopped.
class GatherThenWaitJob final : public imp::detail::ImportJob {
  OXYGEN_TYPED(GatherThenWaitJob)
public:
  using ImportJob::ImportJob;
  ~GatherThenWaitJob() override = default;
  OXYGEN_MAKE_NON_COPYABLE(GatherThenWaitJob)
  OXYGEN_MAKE_NON_MOVABLE(GatherThenWaitJob)
  co::Event waiting;

private:
  auto ExecuteAsync() -> co::Co<imp::ImportReport> override
  {
    EnsureCookedRoot();
    Session().AddDiagnostic(MakeGatheredWarning());
    waiting.Trigger();
    co_await co::kSuspendForever;
    co_return imp::ImportReport {};
  }
};

//! Gathers a diagnostic, then fails with an exception.
class GatherThenThrowJob final : public imp::detail::ImportJob {
  OXYGEN_TYPED(GatherThenThrowJob)
public:
  using ImportJob::ImportJob;
  ~GatherThenThrowJob() override = default;
  OXYGEN_MAKE_NON_COPYABLE(GatherThenThrowJob)
  OXYGEN_MAKE_NON_MOVABLE(GatherThenThrowJob)

private:
  auto ExecuteAsync() -> co::Co<imp::ImportReport> override
  {
    EnsureCookedRoot();
    Session().AddDiagnostic(MakeGatheredWarning());
    if (Session().HasErrors()) {
      co_return imp::ImportReport {};
    }
    throw std::runtime_error("Failure after a gathered diagnostic");
  }
};

//! A plain job: reads its source, runs `after_read`, and emits a material.
class ReadThenEmitJob final : public imp::detail::ImportJob {
  OXYGEN_TYPED(ReadThenEmitJob)
public:
  ReadThenEmitJob(
    imp::detail::ImportJobParams params, std::function<void()> after_read)
    : ImportJob(std::move(params))
    , after_read_(std::move(after_read))
  {
  }
  ~ReadThenEmitJob() override = default;
  OXYGEN_MAKE_NON_COPYABLE(ReadThenEmitJob)
  OXYGEN_MAKE_NON_MOVABLE(ReadThenEmitJob)

private:
  auto ExecuteAsync() -> co::Co<imp::ImportReport> override
  {
    EnsureCookedRoot();
    auto& session = Session();
    const auto bytes = co_await FileReader()->ReadFile(Request().source_path);
    if (!bytes) {
      throw std::runtime_error("The fixture source is unreadable");
    }
    after_read_();
    const auto descriptor = oxygen::cooker::testing::MaterialBuilder {};
    session.AssetEmitter().Emit(
      oxygen::data::AssetKey::FromVirtualPath("/Cooked/source.omat"),
      oxygen::data::AssetType::kMaterial, "/Cooked/source.omat", "source.omat",
      descriptor.Bytes(), descriptor.References());
    co_return co_await session.Finalize();
  }
  std::function<void()> after_read_;
};

//! What a job told its callers, in full.
struct JobOutcome final {
  imp::ImportReport report;
  int completions = 0;
  int final_events = 0;
  imp::ImportPhase final_phase = imp::ImportPhase::kPending;
};

class ImportJobOutcomeTest : public testing::Test {
protected:
  ImportJobOutcomeTest()
    : reader_(imp::CreateAsyncFileReader(loop_.Driver()))
    , pool_(loop_, 1)
    , tables_(loop_.Driver(),
        oxygen::make_observer(&oxygen::content::cooked::RealCookerStorage()))
    , indexes_(
        oxygen::make_observer(&oxygen::content::cooked::RealCookerStorage()))
  {
    std::filesystem::create_directories(Root());
    WriteSource("original");
  }

  [[nodiscard]] auto Root() const -> std::filesystem::path
  {
    return temp_.Path() / "root";
  }

  [[nodiscard]] auto Source() const -> std::filesystem::path
  {
    return temp_.Path() / "source.bin";
  }

  auto WriteSource(const std::string_view content) const -> void
  {
    std::ofstream(Source(), std::ios::binary) << content;
  }

  [[nodiscard]] auto Params() -> imp::detail::ImportJobParams
  {
    imp::ImportRequest request;
    request.source_path = Source();
    request.asset_virtual_path = "/Cooked/source.omat";
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.cooked_root = Root();
    return imp::detail::ImportJobParams {
      .id = imp::ImportJobId { 1 },
      .request = std::move(request),
      .on_complete = [this](auto, const imp::ImportReport& report) -> void {
        outcome_.report = report;
        ++outcome_.completions;
      },
      .on_progress = [this](const imp::ProgressEvent& event) -> void {
        if (event.header.kind == imp::ProgressEventKind::kJobFinished) {
          ++outcome_.final_events;
          outcome_.final_phase = event.header.phase;
        }
      },
      .cancel_event = {},
      .reader = oxygen::make_observer(reader_.get()),
      .driver = oxygen::make_observer(&loop_.Driver()),
      .thread_pool = oxygen::make_observer(&pool_),
      .registry = oxygen::make_observer(&tables_),
      .index_registry = oxygen::make_observer(&indexes_),
      .concurrency = {},
      .script_compile_callback = {},
      .stop_token = {},
      .model_import_record = {},
    };
  }

  [[nodiscard]] auto HasIndex() const -> bool
  {
    return std::filesystem::exists(Root() / "container.index.bin");
  }

  [[nodiscard]] auto Reported(const std::string_view code) const -> bool
  {
    return std::ranges::any_of(outcome_.report.diagnostics,
      [code](const imp::ImportDiagnostic& diagnostic) -> bool {
        return diagnostic.code == code;
      });
  }

  oxygen::testing::ScopedTempDir temp_;
  imp::ImportEventLoop loop_;
  std::unique_ptr<imp::IAsyncFileReader> reader_;
  co::ThreadPool pool_;
  imp::ResourceTableRegistry tables_;
  imp::LooseCookedIndexRegistry indexes_;
  JobOutcome outcome_;
};

//! A cancelled job says so, keeps what it found and ends with one final event.
NOLINT_TEST_F(ImportJobOutcomeTest, CancelledJobKeepsWhatItGathered)
{
  auto job = GatherThenWaitJob(Params());
  co::Run(loop_, [&] -> co::Co<> {
    OXCO_WITH_NURSERY(jobs)
    {
      co_await jobs.Start(&imp::detail::ImportJob::ActivateAsync, &job);
      job.Run();
      co_await job.waiting;
      job.Stop();
      co_await job.Wait();
      co_return co::kJoin;
    };
  });

  EXPECT_FALSE(outcome_.report.success);
  EXPECT_TRUE(outcome_.report.cancelled);
  EXPECT_TRUE(Reported(kGatheredCode));
  EXPECT_TRUE(Reported(imp::import_codes::kCanceled));
  EXPECT_EQ(outcome_.completions, 1);
  EXPECT_EQ(outcome_.final_events, 1);
  EXPECT_EQ(outcome_.final_phase, imp::ImportPhase::kCancelled);
  EXPECT_FALSE(HasIndex());
}

//! A job that throws is failed, not cancelled, and keeps what it found.
NOLINT_TEST_F(ImportJobOutcomeTest, FailedJobKeepsWhatItGathered)
{
  auto job = GatherThenThrowJob(Params());
  co::Run(loop_, RunFailingJob(&job));

  EXPECT_FALSE(outcome_.report.success);
  EXPECT_FALSE(outcome_.report.cancelled);
  EXPECT_TRUE(Reported(kGatheredCode));
  EXPECT_TRUE(Reported(imp::import_codes::kException));
  EXPECT_EQ(outcome_.completions, 1);
  EXPECT_EQ(outcome_.final_events, 1);
  EXPECT_EQ(outcome_.final_phase, imp::ImportPhase::kFailed);
}

//! A plain job with no capture and no record still finds a changed source, and
//! publishes nothing.
NOLINT_TEST_F(ImportJobOutcomeTest, SourceChangedAfterReadPublishesNothing)
{
  auto job = ReadThenEmitJob(
    Params(), [this] -> void { WriteSource("changed under the job"); });
  co::Run(loop_, RunFailingJob(&job));

  EXPECT_FALSE(outcome_.report.success);
  EXPECT_FALSE(outcome_.report.cancelled);
  EXPECT_TRUE(Reported(imp::import_codes::kSourceChanged));
  EXPECT_TRUE(outcome_.report.products.empty());
  EXPECT_FALSE(HasIndex());
  EXPECT_EQ(outcome_.final_phase, imp::ImportPhase::kFailed);
}

//! The same job over an unchanged source publishes.
NOLINT_TEST_F(ImportJobOutcomeTest, UnchangedSourcePublishes)
{
  auto job = ReadThenEmitJob(Params(), [] -> void { });
  co::Run(loop_, RunFailingJob(&job));

  EXPECT_TRUE(outcome_.report.success);
  EXPECT_EQ(outcome_.report.products.size(), 1U);
  EXPECT_TRUE(HasIndex());
  EXPECT_EQ(outcome_.final_phase, imp::ImportPhase::kComplete);
}

//! A progress callback that throws after publication cannot make the job a
//! failure: the report tells what happened to the output.
NOLINT_TEST_F(ImportJobOutcomeTest, ThrowingProgressCannotFailAPublishedJob)
{
  auto params = Params();
  params.on_progress = [](const imp::ProgressEvent&) -> void {
    throw std::runtime_error("A broken progress listener");
  };
  auto job = ReadThenEmitJob(std::move(params), [] -> void { });
  co::Run(loop_, RunFailingJob(&job));

  EXPECT_TRUE(outcome_.report.success);
  EXPECT_FALSE(outcome_.report.cancelled);
  EXPECT_EQ(outcome_.completions, 1);
  EXPECT_TRUE(HasIndex());
}

//! A refusal found before registration is the job's alone: typed, with
//! nothing published.
NOLINT_TEST_F(ImportJobOutcomeTest, RefusalBeforeCommitPublishesNothing)
{
  auto params = Params();
  params.request.source_mount_path = {};
  auto job = ReadThenEmitJob(std::move(params), [] -> void { });
  co::Run(loop_, RunFailingJob(&job));

  EXPECT_FALSE(outcome_.report.success);
  EXPECT_TRUE(Reported(imp::mount_codes::kSourceOutsideMount.id));
  EXPECT_TRUE(outcome_.report.products.empty());
  EXPECT_TRUE(outcome_.report.outputs.empty());
  EXPECT_FALSE(HasIndex());
}

//! A job that never ran still ends with its one final event and completion.
NOLINT_TEST_F(ImportJobOutcomeTest, CancelUnstartedCompletesAsCancelled)
{
  auto job = GatherThenWaitJob(Params());
  job.CancelUnstarted();

  EXPECT_TRUE(outcome_.report.cancelled);
  EXPECT_TRUE(Reported(imp::import_codes::kCanceled));
  EXPECT_EQ(outcome_.completions, 1);
  EXPECT_EQ(outcome_.final_events, 1);
  EXPECT_EQ(outcome_.final_phase, imp::ImportPhase::kCancelled);
}

NOLINT_TEST_F(ImportJobOutcomeTest, RefuseUnstartedCompletesAsFailed)
{
  auto job = GatherThenWaitJob(Params());
  job.RefuseUnstarted(imp::import_codes::kQueueFull, "The queue is full");

  EXPECT_FALSE(outcome_.report.success);
  EXPECT_FALSE(outcome_.report.cancelled);
  EXPECT_TRUE(Reported(imp::import_codes::kQueueFull));
  EXPECT_EQ(outcome_.completions, 1);
  EXPECT_EQ(outcome_.final_events, 1);
  EXPECT_EQ(outcome_.final_phase, imp::ImportPhase::kFailed);
}

} // namespace
