//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Composition/Object.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Import/FileError.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/IAsyncFileWriter.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/ImportJob.h>
#include <Oxygen/Cooker/Import/Internal/ImportJobParams.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedIndexRegistry.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableAggregator.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableRegistry.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace co = oxygen::co;
namespace imp = oxygen::content::import;

class HeldCompletionWriter final : public imp::IAsyncFileWriter {
public:
  explicit HeldCompletionWriter(imp::ImportEventLoop& loop)
    : writer_(imp::CreateAsyncFileWriter(loop))
  {
  }
  ~HeldCompletionWriter() override = default;
  OXYGEN_MAKE_NON_COPYABLE(HeldCompletionWriter)
  OXYGEN_MAKE_NON_MOVABLE(HeldCompletionWriter)

  co::Event callback_held;
  co::Event table_write_held;
  auto HoldTableWrites() -> void { hold_tables_ = true; }
  auto ReleaseTableWrites() -> void
  {
    hold_tables_ = false;
    table_released_.Trigger();
  }
  auto Release() -> void
  {
    hold_ = false;
    if (!pending_) {
      throw std::logic_error("No held write callback");
    }
    auto pending = std::move(*pending_);
    pending_.reset();
    pending.callback(pending.error, pending.bytes);
    released_.Trigger();
  }
  auto Write(const std::filesystem::path& path,
    std::span<const std::byte> bytes, imp::WriteOptions options)
    -> co::Co<oxygen::Result<uint64_t, imp::FileErrorInfo>> override
  {
    if (hold_tables_ && path.extension() == ".table") {
      table_write_held.Trigger();
      co_await table_released_;
    }
    co_return co_await writer_->Write(path, bytes, options);
  }
  auto WriteAt(const std::filesystem::path& path, uint64_t offset,
    std::span<const std::byte> bytes, imp::WriteOptions options)
    -> co::Co<oxygen::Result<uint64_t, imp::FileErrorInfo>> override
  {
    return writer_->WriteAt(path, offset, bytes, options);
  }
  auto WriteAsync(const std::filesystem::path& path,
    std::span<const std::byte> bytes, imp::WriteOptions options,
    imp::WriteCompletionCallback callback) -> void override
  {
    writer_->WriteAsync(path, bytes, options, Gate(std::move(callback)));
  }
  auto WriteAtAsync(const std::filesystem::path& path, uint64_t offset,
    std::span<const std::byte> bytes, imp::WriteOptions options,
    imp::WriteCompletionCallback callback) -> void override
  {
    writer_->WriteAtAsync(
      path, offset, bytes, options, Gate(std::move(callback)));
  }
  auto Flush() -> co::Co<oxygen::Result<void, imp::FileErrorInfo>> override
  {
    const auto first = co_await writer_->Flush();
    if (pending_) {
      co_await released_;
    }
    const auto rest = co_await writer_->Flush();
    co_return first ? rest : first;
  }
  auto CancelAll() -> void override { writer_->CancelAll(); }
  auto PendingCount() const -> size_t override
  {
    return writer_->PendingCount() + (pending_ ? 1U : 0U);
  }

private:
  struct Pending final {
    imp::WriteCompletionCallback callback;
    imp::FileErrorInfo error;
    uint64_t bytes = 0;
  };
  auto Gate(imp::WriteCompletionCallback callback)
    -> imp::WriteCompletionCallback
  {
    return [this, callback = std::move(callback)](
             const imp::FileErrorInfo& error, uint64_t bytes) {
      if (hold_) {
        CHECK_F(!pending_);
        pending_.emplace(
          Pending { .callback = callback, .error = error, .bytes = bytes });
        callback_held.Trigger();
      } else {
        callback(error, bytes);
      }
    };
  }
  std::unique_ptr<imp::IAsyncFileWriter> writer_;
  std::optional<Pending> pending_;
  co::Event released_;
  co::Event table_released_;
  bool hold_tables_ = false;
  bool hold_ = true;
};

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
    co_await co::UntilCancelledAnd([this] { return Exit(); });
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
    oxygen::data::pak::render::MaterialAssetDesc descriptor {};
    descriptor.header.asset_type
      = static_cast<uint8_t>(oxygen::data::AssetType::kMaterial);
    descriptor.header.version
      = oxygen::data::pak::render::kMaterialAssetVersion;
    session.AssetEmitter().Emit(
      oxygen::data::AssetKey::FromVirtualPath("/Content/test.omat"),
      oxygen::data::AssetType::kMaterial, "/Content/test.omat", "test.omat",
      std::as_bytes(std::span { &descriptor, 1 }));
    co_return co_await session.Finalize();
  }
  ProducerState& state_;
};

NOLINT_TEST(
  ImportJobLifetimeTest, CancellationDrainsCallbacksAndAllowsSameRootRetry)
{
  imp::ImportEventLoop loop;
  auto reader = imp::CreateAsyncFileReader(loop);
  HeldCompletionWriter writer(loop);
  co::ThreadPool pool(loop, 1);
  imp::ResourceTableRegistry tables(writer);
  imp::LooseCookedIndexRegistry indexes;
  const auto root
    = std::filesystem::temp_directory_path() / "oxygen_job_lifetime_retry";
  std::filesystem::remove_all(root);
  imp::ImportRequest request;
  request.source_path = root / "source.json";
  request.cooked_root = root;
  unsigned completions = 0;
  imp::ImportReport first_report;
  imp::ImportReport retry_report;
  ProducerState first_state;
  ProducerState retry_state;
  const auto params = [&](imp::ImportCompletionCallback completion) {
    return imp::detail::ImportJobParams {
      .id = imp::ImportJobId { 1 },
      .request = request,
      .on_complete = std::move(completion),
      .on_progress = {},
      .cancel_event = {},
      .reader = oxygen::make_observer(reader.get()),
      .writer = oxygen::make_observer<imp::IAsyncFileWriter>(&writer),
      .thread_pool = oxygen::make_observer(&pool),
      .registry = oxygen::make_observer(&tables),
      .index_registry = oxygen::make_observer(&indexes),
      .concurrency = {},
      .script_compile_callback = {},
      .stop_token = {},
      .retained_import = {},
      .generation_writer = {},
    };
  };
  EmittingJob first(params([&](auto, const auto& report) {
    ++completions;
    first_report = report;
  }),
    first_state);
  EmittingJob retry(params([&](auto, const auto& report) {
    ++completions;
    retry_report = report;
  }),
    retry_state);
  co::Run(loop, [&]() -> co::Co<> {
    OXCO_WITH_NURSERY(jobs)
    {
      co_await jobs.Start(&EmittingJob::ActivateAsync, &first);
      first.Run();
      co_await writer.callback_held;
      first.Stop();
      co_await first_state.destroyed;
      EXPECT_TRUE(first_state.destroyed_after_exit);
      EXPECT_EQ(completions, 0U);
      EXPECT_FALSE(std::filesystem::exists(root / "container.index.bin"));
      writer.Release();
      co_await first.Wait();
      EXPECT_EQ(completions, 1U);
      EXPECT_FALSE(first_report.success);
      EXPECT_EQ(writer.PendingCount(), 0U);
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
  std::filesystem::remove_all(root);
}

NOLINT_TEST(ImportJobLifetimeTest, TableAdmissionWaitsForCompletedOverwrite)
{
  imp::ImportEventLoop loop;
  HeldCompletionWriter writer(loop);
  writer.HoldTableWrites();
  imp::ResourceTableRegistry tables(writer);
  const auto root
    = std::filesystem::temp_directory_path() / "oxygen_table_admission";
  std::filesystem::remove_all(root);
  const imp::LooseCookedLayout layout;
  auto first = tables.BeginSession(root);
  const auto inserted
    = tables.BufferAggregator(root, layout).AcquireOrInsert("first", [] {
        return std::pair { oxygen::data::pak::core::BufferResourceDesc {},
          imp::WriteReservation {} };
      });
  uint32_t second_index = 0;
  bool admitted = false;
  co::Event attempting;
  co::Event finished;
  co::Run(loop, [&]() -> co::Co<> {
    OXCO_WITH_NURSERY(tasks)
    {
      tasks.Start(
        [&]() -> co::Co<> { EXPECT_TRUE(co_await tables.EndSession(first)); });
      co_await writer.table_write_held;
      EXPECT_FALSE(first.IsActive());
      EXPECT_THROW(
        static_cast<void>(tables.BeginSession(root)), std::logic_error);
      tasks.Start([&]() -> co::Co<> {
        attempting.Trigger();
        co_await tables.WaitForFinalization(root);
        admitted = true;
        auto second = tables.BeginSession(root);
        second_index = tables.BufferAggregator(root, layout)
                         .AcquireOrInsert("second",
                           [] {
                             return std::pair {
                               oxygen::data::pak::core::BufferResourceDesc {},
                               imp::WriteReservation {}
                             };
                           })
                         .index;
        EXPECT_TRUE(co_await tables.EndSession(second));
        finished.Trigger();
      });
      co_await attempting;
      EXPECT_FALSE(admitted);
      writer.ReleaseTableWrites();
      co_await finished;
      co_return co::kJoin;
    };
  });
  EXPECT_TRUE(admitted);
  EXPECT_EQ(second_index, inserted.index + 1U);
  std::filesystem::remove_all(root);
}

NOLINT_TEST(
  ImportJobLifetimeTest, IndexParticipantsAwaitPublicationAndCanReenter)
{
  imp::ImportEventLoop loop;
  imp::LooseCookedIndexRegistry indexes;
  const auto root
    = std::filesystem::temp_directory_path() / "oxygen_index_publication";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  auto first = indexes.BeginSession(root, std::nullopt);
  auto last = indexes.BeginSession(root, std::nullopt);
  bool first_completed = false;
  oxygen::data::SourceKey first_key {};
  oxygen::data::SourceKey last_key {};
  co::Event next_turn;
  co::Run(loop, [&]() -> co::Co<> {
    OXCO_WITH_NURSERY(tasks)
    {
      tasks.Start([&]() -> co::Co<> {
        const auto publication = co_await indexes.EndSession(first);
        first_completed = true;
        first_key = publication.source_key;
        EXPECT_FALSE(publication.write_result.has_value());
        EXPECT_TRUE(std::filesystem::exists(root / "container.index.bin"));
        // Publication wakes callbacks outside the root mutex, so they can admit
        // the next cohort without observing or overwriting the old completion.
        auto next = indexes.BeginSession(root, std::nullopt);
        const auto next_publication = co_await indexes.EndSession(next);
        EXPECT_TRUE(next_publication.write_result.has_value());
      });
      loop.Post([&] { next_turn.Trigger(); });
      co_await next_turn;
      EXPECT_FALSE(first.IsActive());
      EXPECT_FALSE(first_completed);
      const auto publication = co_await indexes.EndSession(last);
      EXPECT_TRUE(publication.write_result.has_value());
      last_key = publication.source_key;
      co_return co::kJoin;
    };
  });
  EXPECT_TRUE(first_completed);
  EXPECT_EQ(first_key, last_key);
  EXPECT_FALSE(first.IsActive());
  EXPECT_FALSE(last.IsActive());
  std::filesystem::remove_all(root);
}

NOLINT_TEST(
  ImportJobLifetimeTest, FailedIndexPublicationReachesEveryParticipant)
{
  imp::ImportEventLoop loop;
  imp::LooseCookedIndexRegistry indexes;
  const auto root = std::filesystem::temp_directory_path()
    / "oxygen_index_publication_failure";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  auto first = indexes.BeginSession(root, std::nullopt);
  auto last = indexes.BeginSession(root, std::nullopt);
  std::filesystem::create_directories(root / "container.index.bin");
  uint32_t failures = 0;
  co::Event next_turn;
  co::Run(loop, [&]() -> co::Co<> {
    OXCO_WITH_NURSERY(tasks)
    {
      tasks.Start([&]() -> co::Co<> {
        try {
          static_cast<void>(co_await indexes.EndSession(first));
          ADD_FAILURE() << "A failed publication reached a waiting participant";
        } catch (const std::exception&) {
          ++failures;
        }
      });
      loop.Post([&] { next_turn.Trigger(); });
      co_await next_turn;
      EXPECT_FALSE(first.IsActive());
      EXPECT_EQ(failures, 0U);
      try {
        static_cast<void>(co_await indexes.EndSession(last));
        ADD_FAILURE() << "Replacing a directory with the index must fail";
      } catch (const std::exception&) {
        ++failures;
      }
      co_return co::kJoin;
    };
  });
  EXPECT_EQ(failures, 2U);
  EXPECT_FALSE(first.IsActive());
  EXPECT_FALSE(last.IsActive());
  std::filesystem::remove_all(root);
}

NOLINT_TEST(ImportJobLifetimeTest, LastIndexAbortWakesWaitersAndAllowsRetry)
{
  imp::ImportEventLoop loop;
  imp::LooseCookedIndexRegistry indexes;
  const auto root
    = std::filesystem::temp_directory_path() / "oxygen_index_publication_abort";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  auto first = indexes.BeginSession(root, std::nullopt);
  auto last = indexes.BeginSession(root, std::nullopt);
  bool rejected = false;
  co::Event next_turn;
  co::Run(loop, [&]() -> co::Co<> {
    OXCO_WITH_NURSERY(tasks)
    {
      tasks.Start([&]() -> co::Co<> {
        try {
          static_cast<void>(co_await indexes.EndSession(first));
          ADD_FAILURE() << "An aborted publication cannot succeed";
        } catch (const std::runtime_error&) {
          rejected = true;
        }
      });
      loop.Post([&] { next_turn.Trigger(); });
      co_await next_turn;
      EXPECT_FALSE(first.IsActive());
      EXPECT_FALSE(rejected);
      indexes.AbortSession(last);
      auto retry = indexes.BeginSession(root, std::nullopt);
      const auto publication = co_await indexes.EndSession(retry);
      EXPECT_TRUE(publication.write_result.has_value());
      co_return co::kJoin;
    };
  });
  EXPECT_TRUE(rejected);
  EXPECT_FALSE(first.IsActive());
  EXPECT_FALSE(last.IsActive());
  std::filesystem::remove_all(root);
}

NOLINT_TEST(ImportJobLifetimeTest, CancelledIndexWaiterDoesNotRetireTwice)
{
  imp::ImportEventLoop loop;
  imp::LooseCookedIndexRegistry indexes;
  const auto root
    = std::filesystem::temp_directory_path() / "oxygen_index_waiter_cancel";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  auto first = indexes.BeginSession(root, std::nullopt);
  auto last = indexes.BeginSession(root, std::nullopt);
  co::Event cancel;
  co::Event cancelled;
  co::Event next_turn;
  co::Run(loop, [&]() -> co::Co<> {
    OXCO_WITH_NURSERY(tasks)
    {
      tasks.Start([&]() -> co::Co<> {
        static_cast<void>(
          co_await co::AnyOf(indexes.EndSession(first), cancel));
        cancelled.Trigger();
      });
      loop.Post([&] { next_turn.Trigger(); });
      co_await next_turn;
      EXPECT_FALSE(first.IsActive());
      cancel.Trigger();
      co_await cancelled;
      const auto publication = co_await indexes.EndSession(last);
      EXPECT_TRUE(publication.write_result.has_value());
      co_return co::kJoin;
    };
  });
  EXPECT_FALSE(last.IsActive());
  std::filesystem::remove_all(root);
}

} // namespace
