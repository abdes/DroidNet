//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <ratio>
#include <thread>
#include <utility>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Object.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/Internal/ContentBindingBundle.h>
#include <Oxygen/Content/Internal/ContentPublication.h>
#include <Oxygen/Content/Internal/ContentReleaseQueue.h>
#include <Oxygen/Content/OperationCancelledException.h>
#include <Oxygen/Core/AnyCache.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::CheckoutOwner;
using oxygen::content::internal::ContentReleaseQueue;
using Cache = ContentReleaseQueue::Cache;
constexpr auto kDrainAll = std::numeric_limits<std::size_t>::max();

class TrackedValue final : public oxygen::Object {
  OXYGEN_TYPED(TrackedValue)
public:
  explicit TrackedValue(std::atomic<unsigned int>& destroyed)
    : destroyed_(destroyed)
  {
  }
  ~TrackedValue() override { ++destroyed_; }
  OXYGEN_MAKE_NON_COPYABLE(TrackedValue)
  OXYGEN_MAKE_NON_MOVABLE(TrackedValue)

private:
  std::atomic<unsigned int>& destroyed_;
};

class DifferentValue final : public oxygen::Object {
  OXYGEN_TYPED(DifferentValue)
public:
  DifferentValue() = default;
};

NOLINT_TEST(ContentReleaseQueueTest, PointerCopiesShareOneRequestUsage)
{
  std::atomic<unsigned int> destroyed { 0 };
  Cache cache;
  auto queue = std::make_shared<ContentReleaseQueue>();
  auto acquired = cache.StoreAndAcquire(
    1, std::make_shared<TrackedValue>(destroyed), CheckoutOwner::kExternal);
  if (!acquired) {
    FAIL() << "Expected a published usage";
  }
  auto first = queue->Own(cache, std::move(*acquired));
  auto copy = first;
  first.reset();
  EXPECT_FALSE(queue->HasPending());
  EXPECT_EQ(cache.GetCheckoutCount(1), 2U);
  copy.reset();
  EXPECT_TRUE(queue->HasPending());
  EXPECT_EQ(cache.GetCheckoutCount(1), 2U);
  EXPECT_EQ(queue->Drain(cache, kDrainAll), 1U);
  EXPECT_EQ(cache.GetCheckoutCount(1), 1U);
  EXPECT_TRUE(cache.Remove(1));
  EXPECT_EQ(destroyed.load(), 1U);
}

NOLINT_TEST(ContentReleaseQueueTest, OffThreadDropDefersPayloadAndCacheRelease)
{
  std::atomic<unsigned int> destroyed { 0 };
  Cache cache;
  auto queue = std::make_shared<ContentReleaseQueue>();
  auto value = queue->Own(cache, std::make_shared<TrackedValue>(destroyed));
  std::jthread worker([owned = std::move(value)]() mutable { owned.reset(); });
  worker.join();
  EXPECT_EQ(destroyed.load(), 0U);
  EXPECT_TRUE(queue->HasPending());
  EXPECT_EQ(queue->Drain(cache, 1), 1U);
  EXPECT_EQ(destroyed.load(), 1U);
}

NOLINT_TEST(ContentReleaseQueueTest, FinishesDetachedBatchBeforeNewArrivals)
{
  std::atomic<unsigned int> older_destroyed { 0 };
  std::atomic<unsigned int> newer_destroyed { 0 };
  Cache cache;
  auto queue = std::make_shared<ContentReleaseQueue>();
  for (unsigned int index = 0; index < 4U; ++index) {
    queue->Own(cache, std::make_shared<TrackedValue>(older_destroyed)).reset();
  }
  EXPECT_EQ(queue->Drain(cache, 0), 0U);
  EXPECT_EQ(queue->Drain(cache, 1), 1U);
  for (unsigned int index = 0; index < 4U; ++index) {
    queue->Own(cache, std::make_shared<TrackedValue>(newer_destroyed)).reset();
    EXPECT_EQ(queue->Drain(cache, 1), 1U);
  }
  EXPECT_EQ(older_destroyed.load(), 4U);
  EXPECT_EQ(newer_destroyed.load(), 1U);
  EXPECT_EQ(queue->Drain(cache, kDrainAll), 3U);
  EXPECT_FALSE(queue->HasPending());
}

NOLINT_TEST(ContentReleaseQueueTest, ClearAndReloadRejectOldQueuedUsage)
{
  std::atomic<unsigned int> destroyed { 0 };
  Cache cache;
  auto queue = std::make_shared<ContentReleaseQueue>();
  auto old = cache.StoreAndAcquire(
    1, std::make_shared<TrackedValue>(destroyed), CheckoutOwner::kExternal);
  if (!old) {
    FAIL() << "Expected original publication";
  }
  auto held = queue->Own(cache, std::move(*old));
  cache.Clear();
  auto current = cache.StoreAndAcquire(
    1, std::make_shared<TrackedValue>(destroyed), CheckoutOwner::kInternal);
  if (!current) {
    FAIL() << "Expected replacement publication";
  }
  held.reset();
  EXPECT_EQ(queue->Drain(cache, kDrainAll), 1U);
  EXPECT_EQ(cache.GetCheckoutCount(1), 2U);
  EXPECT_EQ(cache.SnapshotStats().checked_out_internal, 1U);
  EXPECT_EQ(destroyed.load(), 1U);
  EXPECT_TRUE(cache.Release(std::move(current->ticket)));
}

NOLINT_TEST(ContentReleaseQueueTest, LateDropDoesNotRetainOrAccessClosedQueue)
{
  std::atomic<unsigned int> destroyed { 0 };
  Cache cache;
  auto queue = std::make_shared<ContentReleaseQueue>();
  const std::weak_ptr<ContentReleaseQueue> observed = queue;
  auto value = queue->Own(cache, std::make_shared<TrackedValue>(destroyed));
  queue->Close();
  EXPECT_EQ(queue->Drain(cache, kDrainAll), 0U);
  queue.reset();
  EXPECT_TRUE(observed.expired());
  std::jthread worker([owned = std::move(value)]() mutable { owned.reset(); });
  worker.join();
  EXPECT_EQ(destroyed.load(), 1U);
}

NOLINT_TEST(ContentReleaseQueueTest, QueueDestructionDisposesUndrainedRecords)
{
  std::atomic<unsigned int> destroyed { 0 };
  Cache cache;
  auto queue = std::make_shared<ContentReleaseQueue>();
  queue->Own(cache, std::make_shared<TrackedValue>(destroyed)).reset();
  EXPECT_EQ(destroyed.load(), 0U);
  queue.reset();
  EXPECT_EQ(destroyed.load(), 1U);
}

class ShutdownOnDestruction final {
public:
  ShutdownOnDestruction(
    std::shared_ptr<ContentReleaseQueue>& queue, std::unique_ptr<Cache>& cache)
    : queue_(queue)
    , cache_(cache)
  {
  }
  ~ShutdownOnDestruction()
  {
    queue_->Close();
    cache_->Clear();
    EXPECT_EQ(queue_->Drain(*cache_, kDrainAll), 0U);
    cache_.reset();
    queue_.reset();
  }
  OXYGEN_MAKE_NON_COPYABLE(ShutdownOnDestruction)
  OXYGEN_MAKE_NON_MOVABLE(ShutdownOnDestruction)

private:
  std::shared_ptr<ContentReleaseQueue>& queue_;
  std::unique_ptr<Cache>& cache_;
};

NOLINT_TEST(ContentReleaseQueueTest, PayloadCanDestroyLoaderDuringDrain)
{
  std::atomic<unsigned int> destroyed { 0 };
  auto cache = std::make_unique<Cache>();
  auto queue = std::make_shared<ContentReleaseQueue>();
  const std::weak_ptr<ContentReleaseQueue> observed = queue;
  auto acquired = cache->StoreAndAcquire(
    1, std::make_shared<TrackedValue>(destroyed), CheckoutOwner::kExternal);
  if (!acquired) {
    FAIL() << "Expected a published usage";
  }
  queue->Own(*cache, std::move(*acquired)).reset();
  queue->Own(*cache, std::make_shared<ShutdownOnDestruction>(queue, cache))
    .reset();
  EXPECT_EQ(queue->Drain(*cache, kDrainAll), 2U);
  EXPECT_FALSE(cache);
  EXPECT_FALSE(queue);
  EXPECT_TRUE(observed.expired());
  EXPECT_EQ(destroyed.load(), 1U);
}

NOLINT_TEST(ContentReleaseQueueTest, ImmutablePayloadCanBeRetained)
{
  std::atomic<unsigned int> destroyed { 0 };
  Cache cache;
  auto queue = std::make_shared<ContentReleaseQueue>();
  const std::shared_ptr<const TrackedValue> immutable
    = std::make_shared<TrackedValue>(destroyed);
  auto owned = queue->Own(cache, immutable);
  EXPECT_EQ(owned.get(), immutable.get());
  owned.reset();
  EXPECT_EQ(queue->Drain(cache, kDrainAll), 1U);
}

NOLINT_TEST(
  ContentReleaseQueueTest, ControlAllocationFailureReturnsUsageImmediately)
{
  std::atomic<unsigned int> destroyed { 0 };
  Cache cache;
  auto queue
    = std::make_shared<ContentReleaseQueue>(*std::pmr::null_memory_resource());
  auto acquired = cache.StoreAndAcquire(
    1, std::make_shared<TrackedValue>(destroyed), CheckoutOwner::kExternal);
  if (!acquired) {
    FAIL() << "Expected a published usage";
  }
  EXPECT_THROW(
    static_cast<void>(queue->Own(cache, std::move(*acquired))), std::bad_alloc);
  EXPECT_EQ(cache.GetCheckoutCount(1), 1U);
  EXPECT_TRUE(queue->HasPending());
  EXPECT_EQ(queue->Drain(cache, kDrainAll), 1U);
  EXPECT_TRUE(cache.Remove(1));
  EXPECT_EQ(destroyed.load(), 1U);
}

NOLINT_TEST(ContentReleaseQueueTest, CloseRacesWithConcurrentReturns)
{
  constexpr unsigned int kThreads = 4;
  constexpr unsigned int kReturnsPerThread = 128;
  constexpr unsigned int kRounds = 32;
  for (unsigned int round = 0; round < kRounds; ++round) {
    std::atomic<unsigned int> destroyed { 0 };
    Cache cache;
    auto queue = std::make_shared<ContentReleaseQueue>();
    std::barrier start(kThreads + 1);
    std::vector<std::jthread> workers;
    workers.reserve(kThreads);
    for (unsigned int thread = 0; thread < kThreads; ++thread) {
      std::vector<std::shared_ptr<TrackedValue>> values;
      values.reserve(kReturnsPerThread);
      for (unsigned int item = 0; item < kReturnsPerThread; ++item) {
        values.push_back(
          queue->Own(cache, std::make_shared<TrackedValue>(destroyed)));
      }
      workers.emplace_back([&start, owned = std::move(values)]() mutable {
        start.arrive_and_wait();
        owned.clear();
      });
    }
    start.arrive_and_wait();
    queue->Close();
    workers.clear();
    static_cast<void>(queue->Drain(cache, kDrainAll));
    EXPECT_FALSE(queue->HasPending());
    EXPECT_EQ(destroyed.load(), kThreads * kReturnsPerThread);
  }
}

NOLINT_TEST(
  ContentPublicationTest, SharedDecodeCreatesIndependentRequestControls)
{
  using oxygen::content::internal::SharedContentResult;
  std::atomic<unsigned int> destroyed { 0 };
  Cache cache;
  auto queue = std::make_shared<ContentReleaseQueue>();
  auto result = SharedContentResult::Publish(
    cache, *queue, 1, std::make_shared<TrackedValue>(destroyed));
  ASSERT_TRUE(result);
  auto external = result.publication.Acquire<TrackedValue>(
    cache, *queue, CheckoutOwner::kExternal);
  auto internal = result.publication.Acquire<TrackedValue>(
    cache, *queue, CheckoutOwner::kInternal);
  EXPECT_EQ(external.get(), internal.get());
  EXPECT_EQ(cache.GetCheckoutCount(1), 4U);
  result = {};
  EXPECT_EQ(queue->Drain(cache, kDrainAll), 1U);
  EXPECT_EQ(cache.GetCheckoutCount(1), 3U);
  external.reset();
  EXPECT_EQ(queue->Drain(cache, kDrainAll), 1U);
  EXPECT_EQ(cache.SnapshotStats().checked_out_external, 0U);
  EXPECT_EQ(cache.SnapshotStats().checked_out_internal, 1U);
  internal.reset();
  EXPECT_EQ(queue->Drain(cache, kDrainAll), 1U);
  EXPECT_EQ(cache.GetCheckoutCount(1), 1U);
}

NOLINT_TEST(
  ContentPublicationTest, ClearBetweenDeliveriesDoesNotAcquireNewPublication)
{
  using oxygen::content::internal::SharedContentResult;
  std::atomic<unsigned int> old_destroyed { 0 };
  std::atomic<unsigned int> new_destroyed { 0 };
  Cache cache;
  auto queue = std::make_shared<ContentReleaseQueue>();
  auto result = SharedContentResult::Publish(
    cache, *queue, 1, std::make_shared<TrackedValue>(old_destroyed));
  auto first = result.publication.Acquire<TrackedValue>(
    cache, *queue, CheckoutOwner::kExternal);
  cache.Clear();
  auto replacement = SharedContentResult::Publish(
    cache, *queue, 1, std::make_shared<TrackedValue>(new_destroyed));
  auto second = result.publication.Acquire<TrackedValue>(
    cache, *queue, CheckoutOwner::kExternal);
  EXPECT_EQ(first.get(), second.get());
  EXPECT_NE(second.get(), replacement.publication.value.get());
  EXPECT_EQ(cache.GetCheckoutCount(1), 2U);
  result = {};
  first.reset();
  second.reset();
  EXPECT_EQ(queue->Drain(cache, kDrainAll), 3U);
  EXPECT_EQ(old_destroyed.load(), 1U);
  EXPECT_EQ(cache.GetCheckoutCount(1), 2U);
}

NOLINT_TEST(ContentPublicationTest, UncachedResultHasNoResidencyDebit)
{
  using oxygen::content::internal::SharedContentResult;
  std::atomic<unsigned int> destroyed { 0 };
  Cache cache(1);
  auto queue = std::make_shared<ContentReleaseQueue>();
  ASSERT_TRUE(cache.Store(1, std::make_shared<TrackedValue>(destroyed)));
  auto result = SharedContentResult::Publish(
    cache, *queue, 2, std::make_shared<TrackedValue>(destroyed));
  ASSERT_TRUE(result);
  EXPECT_FALSE(result.publication.entry);
  EXPECT_FALSE(result.residency_hold);
  auto delivered = result.publication.Acquire<TrackedValue>(
    cache, *queue, CheckoutOwner::kExternal);
  result = {};
  delivered.reset();
  EXPECT_EQ(queue->Drain(cache, kDrainAll), 1U);
  EXPECT_EQ(cache.GetCheckoutCount(1), 1U);
  EXPECT_FALSE(cache.Contains(2));
  EXPECT_EQ(destroyed.load(), 1U);
}

NOLINT_TEST(ContentPublicationTest, AbandonedDeliveryReturnsTemporaryHold)
{
  using oxygen::content::internal::SharedContentResult;
  std::atomic<unsigned int> destroyed { 0 };
  Cache cache;
  auto queue = std::make_shared<ContentReleaseQueue>();
  {
    const auto result = SharedContentResult::Publish(
      cache, *queue, 1, std::make_shared<TrackedValue>(destroyed));
    EXPECT_TRUE(result);
    EXPECT_EQ(cache.SnapshotStats().pinned_internal, 1U);
  }
  EXPECT_EQ(queue->Drain(cache, kDrainAll), 1U);
  EXPECT_EQ(cache.SnapshotStats().pinned_internal, 0U);
  EXPECT_TRUE(cache.Remove(1));
  EXPECT_EQ(destroyed.load(), 1U);
}

NOLINT_TEST(ContentPublicationTest, ClosedEpochCannotPublishIntoRestartedCache)
{
  using oxygen::content::internal::SharedContentResult;
  std::atomic<unsigned int> destroyed { 0 };
  Cache cache;
  auto retired = std::make_shared<ContentReleaseQueue>();
  retired->Close();
  auto current = std::make_shared<ContentReleaseQueue>();
  EXPECT_THROW(static_cast<void>(SharedContentResult::Publish(cache, *retired,
                 1, std::make_shared<TrackedValue>(destroyed))),
    oxygen::content::OperationCancelledException);
  EXPECT_FALSE(cache.Contains(1));
  EXPECT_EQ(destroyed.load(), 1U);
  EXPECT_TRUE(SharedContentResult::Publish(
    cache, *current, 1, std::make_shared<TrackedValue>(destroyed)));
}

NOLINT_TEST(ContentBindingBundleTest, FailedAttemptsRemainDeduplicated)
{
  auto queue = std::make_shared<ContentReleaseQueue>();
  oxygen::content::internal::ContentBindingBuilder builder(queue);
  const auto key
    = oxygen::data::AssetKey::FromVirtualPath("/Scenes/missing.oscene");
  EXPECT_TRUE(
    builder.TryBeginAsset(key, oxygen::data::MaterialAsset::ClassTypeId()));
  EXPECT_FALSE(
    builder.TryBeginAsset(key, oxygen::data::MaterialAsset::ClassTypeId()));
  EXPECT_TRUE(builder.TryBeginAsset(key, DifferentValue::ClassTypeId()));
  EXPECT_FALSE(builder.FindAsset<oxygen::data::MaterialAsset>(key));
  EXPECT_TRUE(builder.TryBeginResource(oxygen::content::ResourceKey { 1 }));
  EXPECT_FALSE(builder.TryBeginResource(oxygen::content::ResourceKey { 1 }));
  const auto bundle = std::move(builder).Freeze();
  EXPECT_TRUE(bundle->Assets().empty());
  EXPECT_TRUE(bundle->Resources().empty());
}

NOLINT_TEST(
  ContentPublicationTest, TypeMismatchIsRejectedForCachedAndRetiredData)
{
  using oxygen::content::internal::SharedContentResult;
  std::atomic<unsigned int> destroyed { 0 };
  Cache cache;
  auto queue = std::make_shared<ContentReleaseQueue>();
  auto result = SharedContentResult::Publish(
    cache, *queue, 1, std::make_shared<TrackedValue>(destroyed));
  EXPECT_FALSE(result.publication.Acquire<DifferentValue>(
    cache, *queue, CheckoutOwner::kExternal));
  cache.Clear();
  EXPECT_FALSE(result.publication.Acquire<DifferentValue>(
    cache, *queue, CheckoutOwner::kExternal));
  EXPECT_FALSE(queue->HasPending());
}

NOLINT_TEST(ContentPublicationTest, WarmAcquisitionCreatesOnlyOneUsage)
{
  using oxygen::content::internal::ContentAcquisition;
  std::atomic<unsigned int> destroyed { 0 };
  Cache cache;
  auto queue = std::make_shared<ContentReleaseQueue>();
  ASSERT_TRUE(cache.Store(1, std::make_shared<TrackedValue>(destroyed)));
  auto result = ContentAcquisition::FromCache<TrackedValue>(
    cache, *queue, 1, CheckoutOwner::kExternal);
  EXPECT_TRUE(result);
  EXPECT_EQ(cache.GetCheckoutCount(1), 2U);
  EXPECT_EQ(cache.SnapshotStats().pinned_internal, 0U);
  result = {};
  EXPECT_EQ(queue->Drain(cache, kDrainAll), 1U);
}

NOLINT_TEST(ContentBindingBundleTest, ParentRetainsChildrenAcrossCacheClear)
{
  using oxygen::content::internal::BoundResource;
  using oxygen::content::internal::ContentBindingBundle;
  using oxygen::content::internal::SharedContentResult;
  using oxygen::data::MaterialAsset;
  std::atomic<unsigned int> destroyed { 0 };
  Cache cache;
  auto queue = std::make_shared<ContentReleaseQueue>();
  auto result = SharedContentResult::Publish(
    cache, *queue, 1, std::make_shared<TrackedValue>(destroyed));
  const oxygen::content::ResourceKey key { 1 };
  std::vector<BoundResource> resources;
  resources.push_back({ .key = key,
    .publication = result.publication,
    .owner = result.publication.Acquire<TrackedValue>(
      cache, *queue, CheckoutOwner::kInternal) });
  auto parent = std::make_shared<MaterialAsset>(
    oxygen::data::AssetKey {}, oxygen::data::pak::render::MaterialAssetDesc {});
  parent->SetRuntimeBindings(std::make_shared<ContentBindingBundle>(
    std::vector<oxygen::content::internal::BoundAsset> {},
    std::move(resources)));
  cache.Clear();
  result = {};
  static_cast<void>(queue->Drain(cache, kDrainAll));
  EXPECT_EQ(destroyed.load(), 0U);
  auto extracted = parent->GetRuntimeBindings()->FindResource(key);
  ASSERT_TRUE(extracted);
  parent.reset();
  EXPECT_EQ(queue->Drain(cache, kDrainAll), 0U);
  EXPECT_EQ(destroyed.load(), 0U);
  extracted.reset();
  EXPECT_EQ(queue->Drain(cache, kDrainAll), 1U);
  EXPECT_EQ(destroyed.load(), 1U);
}

NOLINT_TEST(ContentOwnershipBenchmark, DISABLED_AcquisitionAndReleaseCost)
{
  using Clock = std::chrono::steady_clock;
  using Nanoseconds = std::chrono::duration<double, std::nano>;
  constexpr std::size_t kRecordsPerBatch = 128;
  constexpr std::size_t kBatches = 512;
  constexpr std::size_t kLargePayloadBytes = 64U * 1024U * 1024U;
  constexpr std::size_t kPayloadSamples = 32;
  std::atomic<unsigned int> destroyed { 0 };
  Cache cache;
  auto queue = std::make_shared<ContentReleaseQueue>();
  ASSERT_TRUE(cache.Store(1, std::make_shared<TrackedValue>(destroyed)));
  std::vector<double> acquisitions;
  std::vector<double> drains;
  acquisitions.reserve(kBatches);
  drains.reserve(kBatches);
  for (std::size_t batch = 0; batch < kBatches; ++batch) {
    const auto start = Clock::now();
    for (std::size_t record = 0; record < kRecordsPerBatch; ++record) {
      auto acquired = cache.Acquire<TrackedValue>(1, CheckoutOwner::kExternal);
      if (!acquired) {
        FAIL() << "Benchmark entry must remain cached";
      }
      queue->Own(cache, std::move(*acquired)).reset();
    }
    const auto queued = Clock::now();
    const auto count = queue->Drain(cache, kRecordsPerBatch);
    const auto returned = Clock::now();
    ASSERT_EQ(count, kRecordsPerBatch);
    acquisitions.push_back(
      Nanoseconds(queued - start).count() / kRecordsPerBatch);
    drains.push_back(Nanoseconds(returned - queued).count());
  }
  std::vector<double> payload_destruction;
  payload_destruction.reserve(kPayloadSamples);
  for (std::size_t sample = 0; sample < kPayloadSamples; ++sample) {
    oxygen::data::pak::core::BufferResourceDesc descriptor {};
    descriptor.size_bytes
      = static_cast<oxygen::data::pak::core::DataBlobSizeT>(kLargePayloadBytes);
    descriptor.element_stride = 1U;
    auto owned = queue->Own(cache,
      std::make_shared<oxygen::data::BufferResource>(
        descriptor, std::vector<uint8_t>(kLargePayloadBytes, 1U)));
    ASSERT_EQ(owned->GetDataSize(), kLargePayloadBytes);
    owned.reset();
    const auto start = Clock::now();
    const auto count = queue->Drain(cache, 1);
    const auto elapsed = Nanoseconds(Clock::now() - start).count();
    ASSERT_EQ(count, 1U);
    payload_destruction.push_back(elapsed);
  }
  std::ranges::sort(acquisitions);
  std::ranges::sort(drains);
  std::ranges::sort(payload_destruction);
  constexpr auto kMedian = kBatches / 2;
  constexpr auto kP95 = kBatches * 95 / 100;
  constexpr auto kNanosecondsPerMicrosecond = 1000.0;
  std::cout << "ownership_benchmark warm_acquire_and_enqueue_median_ns="
            << acquisitions.at(kMedian) << " batch_128_median_us="
            << drains.at(kMedian) / kNanosecondsPerMicrosecond
            << " batch_128_p95_us="
            << drains.at(kP95) / kNanosecondsPerMicrosecond
            << " buffer_64MiB_destroy_median_us="
            << payload_destruction.at(kPayloadSamples / 2)
      / kNanosecondsPerMicrosecond
            << " buffer_64MiB_destroy_max_us="
            << payload_destruction.back() / kNanosecondsPerMicrosecond << '\n';
}

} // namespace
