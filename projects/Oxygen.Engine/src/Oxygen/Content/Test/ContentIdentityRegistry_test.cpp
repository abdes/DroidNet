//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <ratio>
#include <stdexcept>
#include <unordered_set>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/Internal/ContentIdentity.h>
#include <Oxygen/Content/Internal/ContentIdentityRegistry.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/SourceOrigin.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::internal {
namespace {

  class TrackedMemory final : public std::pmr::memory_resource {
  public:
    TrackedMemory() = default;
    ~TrackedMemory() override = default;
    OXYGEN_MAKE_NON_COPYABLE(TrackedMemory)
    OXYGEN_MAKE_NON_MOVABLE(TrackedMemory)

    size_t live_bytes = 0;
    size_t allocations = 0;
    size_t allocation_limit = std::numeric_limits<size_t>::max();

  private:
    auto do_allocate(size_t bytes, size_t alignment) -> void* override
    {
      if (allocations == allocation_limit) {
        throw std::bad_alloc {};
      }
      auto* memory
        = std::pmr::new_delete_resource()->allocate(bytes, alignment);
      live_bytes += bytes;
      ++allocations;
      return memory;
    }

    auto do_deallocate(void* memory, size_t bytes, size_t alignment)
      -> void override
    {
      live_bytes -= bytes;
      std::pmr::new_delete_resource()->deallocate(memory, bytes, alignment);
    }

    auto do_is_equal(const std::pmr::memory_resource& other) const noexcept
      -> bool override
    {
      return this == &other;
    }
  };

  auto CollidingHash(const ContentIdentity&) noexcept -> size_t { return 0; }

  auto MakeAssetKey(const uint8_t suffix) -> data::AssetKey
  {
    data::AssetKey::ByteArray bytes {};
    bytes.back() = suffix;
    return data::AssetKey::FromBytes(bytes);
  }

  NOLINT_TEST(
    ContentIdentityRegistry, BindingViewsSeparateGraphsAndRetainPhysicalKeys)
  {
    ContentIdentityRegistry registry;
    const auto first
      = std::make_shared<const BindingViewId>(BindingViewId { 1 });
    const auto second
      = std::make_shared<const BindingViewId>(BindingViewId { 2 });
    const auto source = data::SourceInstanceId { 1 };
    const auto key = MakeAssetKey(1);
    const auto first_id
      = registry.Intern(AssetIdentity { source, key, *first }, first);
    const auto second_id
      = registry.Intern(AssetIdentity { source, key, *second }, second);
    EXPECT_NE(first_id, second_id);
    EXPECT_EQ(registry.Find(AssetIdentity { source, key, *first }), first_id);
    EXPECT_EQ(registry.Find(AssetIdentity { source, key, *second }), second_id);
    const auto physical
      = CookedResourceIdentity { source, ResourceKind::kTexture, 1 };
    const auto resource = registry.Intern(physical);
    EXPECT_EQ(registry.Intern(physical), resource);
    EXPECT_THROW(static_cast<void>(registry.Intern(
                   AssetIdentity { source, key, *first }, second)),
      std::invalid_argument);
  }

  NOLINT_TEST(
    ContentIdentityRegistry, ExpiredViewLocatorsAreReclaimedInBoundedBatches)
  {
    constexpr uint8_t kAssetCount = 16;
    constexpr size_t kBudget = 3;
    ContentIdentityRegistry registry;
    auto view = std::make_shared<const BindingViewId>(BindingViewId { 1 });
    const auto source = data::SourceInstanceId { 1 };
    const auto physical
      = CookedResourceIdentity { source, ResourceKind::kTexture, 1 };
    const auto resource = registry.Intern(physical);
    for (uint8_t index = 0; index < kAssetCount; ++index) {
      static_cast<void>(registry.Intern(
        AssetIdentity {
          source, MakeAssetKey(static_cast<uint8_t>(index + 1U)), *view },
        view));
    }
    static_cast<void>(registry.ProcessExpiredViews(kBudget));
    EXPECT_EQ(registry.Size(), kAssetCount + 1U);
    view.reset();
    EXPECT_LE(registry.ProcessExpiredViews(kBudget), kBudget);
    EXPECT_LT(registry.Size(), kAssetCount + 1U);
    EXPECT_GT(registry.Size(), 1U);
    static_cast<void>(
      registry.ProcessExpiredViews(std::numeric_limits<size_t>::max()));
    EXPECT_EQ(registry.Size(), 1U);
    EXPECT_EQ(registry.Find(physical), resource);
  }

  NOLINT_TEST(ContentIdentityRegistry, FullIdentitySeparatesEqualHashes)
  {
    ContentIdentityRegistry registry(
      *std::pmr::get_default_resource(), CollidingHash);
    const std::array<ContentIdentity, 8> identities {
      AssetIdentity { data::SourceInstanceId { 1 }, MakeAssetKey(1) },
      AssetIdentity { data::SourceInstanceId { 1 }, MakeAssetKey(2) },
      AssetIdentity { data::SourceInstanceId { 2 }, MakeAssetKey(1) },
      CookedResourceIdentity {
        data::SourceInstanceId { 1 }, ResourceKind::kBuffer, 1 },
      CookedResourceIdentity {
        data::SourceInstanceId { 1 }, ResourceKind::kTexture, 1 },
      CookedResourceIdentity {
        data::SourceInstanceId { 2 }, ResourceKind::kBuffer, 1 },
      SyntheticResourceIdentity { ResourceKind::kBuffer, 1 },
      SyntheticResourceIdentity { ResourceKind::kTexture, 1 },
    };

    std::unordered_set<ContentId> ids;
    for (const auto& identity : identities) {
      const auto id = registry.Intern(identity);
      EXPECT_NE(id, ContentId {});
      EXPECT_TRUE(ids.insert(id).second);
      EXPECT_EQ(registry.Intern(identity), id);
      EXPECT_EQ(registry.Find(identity), id);
      const auto* resolved = registry.Resolve(id);
      ASSERT_NE(resolved, nullptr);
      EXPECT_EQ(*resolved, identity);
    }
    EXPECT_EQ(registry.Size(), identities.size());
  }

  NOLINT_TEST(ContentIdentityRegistry, LookupDoesNotRegisterMissingLocators)
  {
    ContentIdentityRegistry registry;
    const ContentIdentity identity
      = CookedResourceIdentity { data::SourceInstanceId { 1 },
          ResourceKind::kPhysics, 3 };
    EXPECT_EQ(registry.Find(identity), ContentId {});
    EXPECT_EQ(registry.Resolve(ContentId { 1 }), nullptr);
    EXPECT_EQ(registry.Size(), 0U);
  }

  NOLINT_TEST(ContentIdentityRegistry, LocatorAddressesSurviveRehash)
  {
    ContentIdentityRegistry registry;
    const ContentIdentity identity
      = AssetIdentity { data::SourceInstanceId { 1 }, MakeAssetKey(1) };
    const auto id = registry.Intern(identity);
    const auto* locator = registry.Resolve(id);
    constexpr uint32_t kResourceCount = 4096;
    for (uint32_t index = 1; index <= kResourceCount; ++index) {
      static_cast<void>(registry.Intern(CookedResourceIdentity {
        data::SourceInstanceId { 1 }, ResourceKind::kTexture, index }));
    }
    EXPECT_EQ(registry.Resolve(id), locator);
    ASSERT_NE(locator, nullptr);
    EXPECT_EQ(*locator, identity);
    EXPECT_EQ(registry.Entries().size(), registry.Size());
  }

  NOLINT_TEST(ContentIdentityRegistry, RemovedAndClearedIdsAreNeverReused)
  {
    ContentIdentityRegistry registry;
    const ContentIdentity identity
      = SyntheticResourceIdentity { ResourceKind::kTexture, 1 };
    const auto first = registry.Intern(identity);
    registry.Erase(first);
    EXPECT_EQ(registry.Find(identity), ContentId {});
    EXPECT_EQ(registry.Resolve(first), nullptr);
    const auto second = registry.Intern(identity);
    EXPECT_GT(second.get(), first.get());
    registry.Clear();
    EXPECT_EQ(registry.Resolve(second), nullptr);
    EXPECT_TRUE(registry.Entries().empty());
    const auto third = registry.Intern(identity);
    EXPECT_GT(third.get(), second.get());
    registry.Erase(first);
    EXPECT_EQ(registry.Find(identity), third);
  }

  NOLINT_TEST(ContentIdentityRegistry, RetirementBoundsLiveLocatorStorage)
  {
    TrackedMemory memory;
    ContentIdentityRegistry registry(memory);
    size_t retained_bytes = 0;
    constexpr uint64_t kGenerations = 256;
    constexpr uint32_t kLocatorsPerGeneration = 32;
    std::array<ContentId, kLocatorsPerGeneration> previous {};
    for (uint64_t generation = 1; generation <= kGenerations; ++generation) {
      for (const auto id : previous) {
        registry.Erase(id);
      }
      uint32_t index = 1;
      for (auto& id : previous) {
        id = registry.Intern(
          CookedResourceIdentity { data::SourceInstanceId { generation },
            ResourceKind::kBuffer, index++ });
      }
      if (generation == 1) {
        retained_bytes = memory.live_bytes;
      }
      EXPECT_EQ(memory.live_bytes, retained_bytes);
      EXPECT_EQ(registry.Size(), kLocatorsPerGeneration);
      EXPECT_EQ(registry.Entries().size(), kLocatorsPerGeneration);
    }
  }

  NOLINT_TEST(ContentIdentityRegistry, SourceRetirementPreservesOtherLocators)
  {
    ContentIdentityRegistry registry;
    const auto retired = registry.Intern(
      AssetIdentity { data::SourceInstanceId { 1 }, MakeAssetKey(1) });
    const auto live = registry.Intern(CookedResourceIdentity {
      data::SourceInstanceId { 2 }, ResourceKind::kTexture, 1 });
    const auto synthetic
      = registry.Intern(SyntheticResourceIdentity { ResourceKind::kBuffer, 1 });
    registry.EraseSources({ data::SourceInstanceId { 1 } });
    EXPECT_EQ(registry.Resolve(retired), nullptr);
    EXPECT_NE(registry.Resolve(live), nullptr);
    EXPECT_NE(registry.Resolve(synthetic), nullptr);
    EXPECT_EQ(registry.Size(), 2U);
  }

  NOLINT_TEST(ContentIdentityRegistry, ExistingAndMissingLookupsDoNotAllocate)
  {
    TrackedMemory memory;
    ContentIdentityRegistry registry(memory);
    const ContentIdentity identity
      = SyntheticResourceIdentity { ResourceKind::kBuffer, 1 };
    const auto id = registry.Intern(identity);
    memory.allocation_limit = memory.allocations;
    EXPECT_EQ(registry.Find(identity), id);
    EXPECT_EQ(registry.Intern(identity), id);
    EXPECT_NE(registry.Resolve(id), nullptr);
    EXPECT_EQ(
      registry.Find(SyntheticResourceIdentity { ResourceKind::kBuffer, 2 }),
      ContentId {});
  }

  NOLINT_TEST(ContentIdentityRegistry, FailedIndexAllocationRollsBackTheLocator)
  {
    TrackedMemory memory;
    ContentIdentityRegistry registry(memory);
    const ContentIdentity first
      = SyntheticResourceIdentity { ResourceKind::kBuffer, 1 };
    const ContentIdentity second
      = SyntheticResourceIdentity { ResourceKind::kBuffer, 2 };
    const auto id = registry.Intern(first);
    const auto bytes = memory.live_bytes;
    memory.allocation_limit = memory.allocations + 1;
    EXPECT_THROW(static_cast<void>(registry.Intern(second)), std::bad_alloc);
    EXPECT_EQ(registry.Size(), 1U);
    EXPECT_EQ(registry.Find(first), id);
    EXPECT_EQ(registry.Find(second), ContentId {});
    EXPECT_EQ(memory.live_bytes, bytes);
    memory.allocation_limit = std::numeric_limits<size_t>::max();
    EXPECT_NE(registry.Intern(second), id);
  }

  NOLINT_TEST(ContentIdentityBenchmark, DISABLED_LocatorCost)
  {
    constexpr uint32_t kCount = 65536;
    constexpr uint32_t kLookupRepeats = 16;
    TrackedMemory memory;
    ContentIdentityRegistry registry(memory);
    const auto start = std::chrono::steady_clock::now();
    for (uint32_t index = 1; index <= kCount; ++index) {
      static_cast<void>(registry.Intern(CookedResourceIdentity {
        data::SourceInstanceId { 1 }, ResourceKind::kTexture, index }));
    }
    const auto inserted = std::chrono::steady_clock::now();
    const auto cooked_bytes = memory.live_bytes;
    const auto allocations = memory.allocations;
    uint64_t checksum = 0;
    for (uint32_t repeat = 0; repeat < kLookupRepeats; ++repeat) {
      for (uint32_t index = 1; index <= kCount; ++index) {
        checksum
          += registry
               .Find(CookedResourceIdentity {
                 data::SourceInstanceId { 1 }, ResourceKind::kTexture, index })
               .get();
      }
    }
    const auto looked_up = std::chrono::steady_clock::now();
    EXPECT_EQ(memory.allocations, allocations);
    for (uint32_t serial = 1; serial <= kCount; ++serial) {
      static_cast<void>(registry.Intern(
        SyntheticResourceIdentity { ResourceKind::kTexture, serial }));
    }
    registry.EraseSources({ data::SourceInstanceId { 1 } });
    const auto synthetic_bytes = memory.live_bytes;
    registry.Clear();
    const auto empty_bytes = memory.live_bytes;
    using Nanoseconds = std::chrono::duration<double, std::nano>;
    std::cout << "locators=" << kCount
              << " intern_ns=" << Nanoseconds(inserted - start).count() / kCount
              << " lookup_ns="
              << Nanoseconds(looked_up - inserted).count()
        / (kCount * kLookupRepeats)
              << " cooked_allocator_bytes=" << cooked_bytes
              << " synthetic_allocator_bytes_after_cooked_retirement="
              << synthetic_bytes
              << " empty_retained_bucket_bytes=" << empty_bytes
              << " checksum=" << checksum << '\n';
  }

} // namespace
} // namespace oxygen::content::internal
