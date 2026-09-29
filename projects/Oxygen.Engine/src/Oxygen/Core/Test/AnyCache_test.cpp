//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Composition/Object.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Core/AnyCache.h>
#include <Oxygen/Core/RefCountedEviction.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::AnyCache;

namespace {

// NOLINTBEGIN(*-magic-numbers)

// -----------------------------------------------------------------------------
// Basic test cases
// -----------------------------------------------------------------------------

class AnyCacheBasicTest : public testing::Test {
protected:
  struct CachedNumber final : oxygen::Object {
    OXYGEN_TYPED(CachedNumber)
  public:
    explicit CachedNumber(const int v)
      : value(v)
    {
    }
    int value { 0 };
  };

  struct CachedString final : oxygen::Object {
    OXYGEN_TYPED(CachedString)
  public:
    explicit CachedString(const std::string_view v)
      : value(v)
    {
    }
    std::string value;
  };

  // Create a cache with a small budget
  AnyCache<int, oxygen::RefCountedEviction<int>> cache_;
};

NOLINT_TEST_F(AnyCacheBasicTest, ExactTicketsPreserveRequestRoles)
{
  ASSERT_TRUE(cache_.Store(1, std::make_shared<CachedNumber>(1)));
  auto internal
    = cache_.Acquire<CachedNumber>(1, oxygen::CheckoutOwner::kInternal);
  auto external
    = cache_.Acquire<CachedNumber>(1, oxygen::CheckoutOwner::kExternal);
  auto pin = cache_.AcquirePin(1, oxygen::CheckoutOwner::kExternal);
  if (!internal || !external || !pin) {
    FAIL() << "All usages must be admitted";
  }
  EXPECT_TRUE(cache_.Release(std::move(internal->ticket)));
  auto stats = cache_.SnapshotStats();
  EXPECT_EQ(stats.checked_out_internal, 0U);
  EXPECT_EQ(stats.checked_out_external, 1U);
  EXPECT_EQ(stats.pinned_external, 1U);
  EXPECT_FALSE(cache_.Release(std::move(internal->ticket)));
  EXPECT_TRUE(cache_.Release(std::move(*pin)));
  EXPECT_TRUE(cache_.Release(std::move(external->ticket)));
  EXPECT_EQ(cache_.GetCheckoutCount(1), 1U);
}

NOLINT_TEST_F(AnyCacheBasicTest, OldTicketCannotReleaseReloadedEntry)
{
  ASSERT_TRUE(cache_.Store(1, std::make_shared<CachedNumber>(1)));
  auto old = cache_.Acquire<CachedNumber>(1, oxygen::CheckoutOwner::kExternal);
  if (!old) {
    FAIL() << "Expected the old entry";
  }
  const auto old_entry = old->ticket.Entry();
  cache_.Clear();
  ASSERT_TRUE(cache_.Store(1, std::make_shared<CachedNumber>(2)));
  auto current
    = cache_.Acquire<CachedNumber>(1, oxygen::CheckoutOwner::kExternal);
  if (!current) {
    FAIL() << "Expected the new entry";
  }
  EXPECT_FALSE(
    cache_.Acquire<CachedNumber>(old_entry, oxygen::CheckoutOwner::kExternal));
  EXPECT_FALSE(cache_.Release(std::move(old->ticket)));
  EXPECT_EQ(cache_.GetCheckoutCount(1), 2U);
  EXPECT_EQ(old->value->value, 1);
  EXPECT_EQ(current->value->value, 2);
  EXPECT_TRUE(cache_.Release(std::move(current->ticket)));
}

NOLINT_TEST_F(AnyCacheBasicTest, TicketRejectsAnotherCacheWithoutConsumingUsage)
{
  AnyCache<int, oxygen::RefCountedEviction<int>> other;
  ASSERT_TRUE(cache_.Store(1, std::make_shared<CachedNumber>(1)));
  ASSERT_TRUE(other.Store(1, std::make_shared<CachedNumber>(2)));
  auto acquired
    = cache_.Acquire<CachedNumber>(1, oxygen::CheckoutOwner::kExternal);
  if (!acquired) {
    FAIL() << "Expected a usage ticket";
  }
  EXPECT_FALSE(other.Release(std::move(acquired->ticket)));
  EXPECT_EQ(other.GetCheckoutCount(1), 1U);
  EXPECT_TRUE(cache_.Release(std::move(acquired->ticket)));
}

NOLINT_TEST_F(AnyCacheBasicTest, ReplacementChangesOnlySuccessfulIncarnations)
{
  ASSERT_TRUE(cache_.Store(1, std::make_shared<CachedNumber>(1)));
  auto acquired
    = cache_.Acquire<CachedNumber>(1, oxygen::CheckoutOwner::kExternal);
  if (!acquired) {
    FAIL() << "Expected an initial ticket";
  }
  const auto original = acquired->ticket.Entry();
  EXPECT_FALSE(cache_.Replace(1, std::make_shared<CachedNumber>(2)));
  auto same
    = cache_.Acquire<CachedNumber>(original, oxygen::CheckoutOwner::kInternal);
  if (!same) {
    FAIL() << "Failed replacement must preserve the incarnation";
  }
  EXPECT_TRUE(cache_.Release(std::move(same->ticket)));
  EXPECT_TRUE(cache_.Release(std::move(acquired->ticket)));
  EXPECT_TRUE(cache_.Replace(1, std::make_shared<CachedNumber>(2)));
  EXPECT_FALSE(
    cache_.Acquire<CachedNumber>(original, oxygen::CheckoutOwner::kExternal));
  EXPECT_TRUE(cache_.Contains<CachedNumber>(1));
  EXPECT_FALSE(cache_.Contains<CachedString>(1));
  EXPECT_FALSE(
    cache_.Acquire<CachedString>(1, oxygen::CheckoutOwner::kExternal));
  EXPECT_EQ(cache_.GetCheckoutCount(1), 1U);
}

NOLINT_TEST_F(AnyCacheBasicTest, InvalidationRetiresAnActivelyLeasedPublication)
{
  auto acquired = cache_.StoreAndAcquire(
    1, std::make_shared<CachedNumber>(1), oxygen::CheckoutOwner::kExternal);
  if (!acquired) {
    FAIL() << "Expected an initial acquisition";
  }
  const auto previous = acquired->ticket.Entry();
  EXPECT_TRUE(cache_.Invalidate(1));
  EXPECT_FALSE(cache_.Contains(previous));
  EXPECT_EQ(acquired->value->value, 1);
  auto replacement = cache_.StoreAndAcquire(
    1, std::make_shared<CachedNumber>(2), oxygen::CheckoutOwner::kExternal);
  if (!replacement) {
    FAIL() << "Expected replacement publication";
  }
  EXPECT_FALSE(cache_.Release(std::move(acquired->ticket)));
  EXPECT_EQ(cache_.GetCheckoutCount(1), 2U);
  EXPECT_TRUE(cache_.Release(std::move(replacement->ticket)));
}

NOLINT_TEST_F(AnyCacheBasicTest, NotificationScopeSurvivesCacheDestruction)
{
  using Cache = AnyCache<int, oxygen::RefCountedEviction<int>>;
  auto cache = std::make_unique<Cache>();
  ASSERT_TRUE(cache->Store(1, std::make_shared<CachedNumber>(1)));
  ASSERT_TRUE(cache->Store(2, std::make_shared<CachedNumber>(2)));
  unsigned int calls = 0;
  auto scope = cache->OnEviction([&](const auto&) {
    ++calls;
    cache.reset();
  });
  cache->Clear();
  EXPECT_FALSE(cache);
  EXPECT_EQ(calls, 1U);
}

NOLINT_TEST_F(AnyCacheBasicTest, StorePeekAndReplaceRespectTypes)
{
  ASSERT_TRUE(cache_.Store(1, std::make_shared<CachedNumber>(1)));
  ASSERT_TRUE(cache_.Store(2, std::make_shared<CachedString>("two")));
  EXPECT_EQ(cache_.Size(), 2U);
  EXPECT_EQ(cache_.Peek<CachedNumber>(1)->value, 1);
  EXPECT_EQ(cache_.Peek<CachedString>(2)->value, "two");
  EXPECT_FALSE(cache_.Peek<CachedString>(1));
  EXPECT_FALSE(cache_.Peek<CachedNumber>(3));
  EXPECT_EQ(cache_.GetTypeId(3), oxygen::kInvalidTypeId);
  EXPECT_FALSE(cache_.Replace(3, std::make_shared<CachedNumber>(3)));
  EXPECT_TRUE(cache_.Replace(1, std::make_shared<CachedString>("one")));
  EXPECT_FALSE(cache_.Contains<CachedNumber>(1));
  EXPECT_TRUE(cache_.Contains<CachedString>(1));
  EXPECT_TRUE(cache_.Store(1, std::make_shared<CachedNumber>(4)));
  EXPECT_EQ(cache_.Peek<CachedNumber>(1)->value, 4);
  EXPECT_TRUE(cache_.Remove(1));
  EXPECT_FALSE(cache_.Remove(1));
}

NOLINT_TEST_F(AnyCacheBasicTest, NullStorageHasNoTypedAcquisition)
{
  EXPECT_TRUE(cache_.Store(1, std::shared_ptr<CachedNumber> {}));
  EXPECT_TRUE(cache_.Contains(1));
  EXPECT_EQ(cache_.GetTypeId(1), oxygen::kInvalidTypeId);
  EXPECT_FALSE(
    cache_.Acquire<CachedNumber>(1, oxygen::CheckoutOwner::kExternal));
  EXPECT_FALSE(cache_.StoreAndAcquire(
    2, std::shared_ptr<CachedNumber> {}, oxygen::CheckoutOwner::kExternal));
  EXPECT_FALSE(cache_.Contains(2));
}

NOLINT_TEST_F(AnyCacheBasicTest, ActiveUsageBlocksPolicyEvictionAndReplacement)
{
  auto acquired = cache_.StoreAndAcquire(
    1, std::make_shared<CachedNumber>(1), oxygen::CheckoutOwner::kExternal);
  if (!acquired) {
    FAIL() << "Expected an acquisition";
  }
  EXPECT_FALSE(cache_.Remove(1));
  EXPECT_FALSE(cache_.Store(1, std::make_shared<CachedNumber>(2)));
  EXPECT_FALSE(cache_.Replace(1, std::make_shared<CachedNumber>(2)));
  EXPECT_EQ(acquired->value->value, 1);
  EXPECT_TRUE(cache_.Release(std::move(acquired->ticket)));
  EXPECT_TRUE(cache_.Remove(1));
  EXPECT_EQ(acquired->value->value, 1);
}

NOLINT_TEST_F(AnyCacheBasicTest, MissingKeysDoNotCreateTickets)
{
  EXPECT_FALSE(
    cache_.Acquire<CachedNumber>(1, oxygen::CheckoutOwner::kInternal));
  EXPECT_FALSE(cache_.AcquirePin(1, oxygen::CheckoutOwner::kExternal));
  EXPECT_FALSE(cache_.Invalidate(1));
  EXPECT_FALSE(cache_.IsCheckedOut(1));
  EXPECT_EQ(cache_.GetCheckoutCount(1), 0U);
  EXPECT_EQ(cache_.Size(), 0U);
}

NOLINT_TEST_F(AnyCacheBasicTest, TicketMoveTransfersOnlyOneUsage)
{
  ASSERT_TRUE(cache_.Store(1, std::make_shared<CachedNumber>(1)));
  auto pin = cache_.AcquirePin(1, oxygen::CheckoutOwner::kExternal);
  if (!pin) {
    FAIL() << "Expected a residency pin";
  }
  auto moved = std::move(*pin);
  EXPECT_FALSE(pin->IsActive());
  EXPECT_FALSE(cache_.Release(std::move(*pin)));
  EXPECT_EQ(cache_.GetCheckoutCount(1), 2U);
  EXPECT_TRUE(cache_.Release(std::move(moved)));
  EXPECT_FALSE(cache_.Release(std::move(moved)));
  EXPECT_EQ(cache_.GetCheckoutCount(1), 1U);
}

NOLINT_TEST_F(AnyCacheBasicTest, PeekPreservesStorageWithoutAddingResidency)
{
  ASSERT_TRUE(cache_.Store(1, std::make_shared<CachedNumber>(1)));
  const auto value = cache_.Peek<CachedNumber>(1);
  EXPECT_EQ(cache_.GetCheckoutCount(1), 1U);
  EXPECT_TRUE(cache_.Remove(1));
  EXPECT_EQ(value->value, 1);
}

NOLINT_TEST_F(AnyCacheBasicTest, KeySnapshotsAndRangesReflectCurrentEntries)
{
  ASSERT_TRUE(cache_.Store(1, std::make_shared<CachedNumber>(1)));
  ASSERT_TRUE(cache_.Store(2, std::make_shared<CachedString>("two")));
  EXPECT_THAT(cache_.KeysSnapshot(), ::testing::UnorderedElementsAre(1, 2));
  std::vector<int> keys;
  for (const auto key : cache_.Keys()) {
    keys.push_back(key);
  }
  EXPECT_THAT(keys, ::testing::UnorderedElementsAre(1, 2));
  cache_.Clear();
  EXPECT_TRUE(cache_.KeysSnapshot().empty());
}

NOLINT_TEST_F(
  AnyCacheBasicTest, RetirementCarriesOriginalDataAndExactIncarnation)
{
  auto original = cache_.StoreAndAcquire(
    1, std::make_shared<CachedNumber>(1), oxygen::CheckoutOwner::kExternal);
  if (!original) {
    FAIL() << "Expected initial publication";
  }
  const auto stamp = original->ticket.Entry();
  EXPECT_TRUE(cache_.Release(std::move(original->ticket)));
  unsigned int calls = 0;
  auto scope = cache_.OnEviction([&](const auto& retired) {
    ++calls;
    EXPECT_EQ(retired.entry, stamp);
    EXPECT_EQ(std::static_pointer_cast<CachedNumber>(retired.value)->value, 1);
    EXPECT_EQ(retired.type, CachedNumber::ClassTypeId());
    EXPECT_EQ(cache_.Peek<CachedNumber>(1)->value, 2);
  });
  EXPECT_TRUE(cache_.Store(1, std::make_shared<CachedNumber>(2)));
  EXPECT_EQ(calls, 1U);
}

NOLINT_TEST_F(AnyCacheBasicTest, ClearRetiresLeasedItemsAndNotifiesOutsideLock)
{
  auto first = cache_.StoreAndAcquire(
    1, std::make_shared<CachedNumber>(1), oxygen::CheckoutOwner::kExternal);
  ASSERT_TRUE(cache_.Store(2, std::make_shared<CachedNumber>(2)));
  if (!first) {
    FAIL() << "Expected initial publication";
  }
  std::vector<int> retired_keys;
  auto scope = cache_.OnEviction([&](const auto& retired) {
    retired_keys.push_back(retired.entry.GetKey());
    EXPECT_EQ(cache_.Size(), 0U);
  });
  cache_.Clear();
  EXPECT_THAT(retired_keys, ::testing::UnorderedElementsAre(1, 2));
  EXPECT_FALSE(cache_.Release(std::move(first->ticket)));
  EXPECT_EQ(first->value->value, 1);
}

NOLINT_TEST_F(AnyCacheBasicTest, NotificationScopesRestorePreviousRegistration)
{
  unsigned int outer_calls = 0;
  unsigned int inner_calls = 0;
  auto outer = cache_.OnEviction([&](const auto&) { ++outer_calls; });
  {
    auto inner = cache_.OnEviction([&](const auto&) { ++inner_calls; });
    ASSERT_TRUE(cache_.Store(1, std::make_shared<CachedNumber>(1)));
    EXPECT_TRUE(cache_.Remove(1));
  }
  ASSERT_TRUE(cache_.Store(1, std::make_shared<CachedNumber>(1)));
  EXPECT_TRUE(cache_.Remove(1));
  EXPECT_EQ(outer_calls, 1U);
  EXPECT_EQ(inner_calls, 1U);
}

NOLINT_TEST_F(AnyCacheBasicTest, BudgetsAndRoleStatsDescribeResidency)
{
  using Cache = AnyCache<int, oxygen::RefCountedEviction<int>>;
  EXPECT_THROW(Cache(0), std::invalid_argument);
  Cache cache(1);
  auto first = cache.StoreAndAcquire(
    1, std::make_shared<CachedNumber>(1), oxygen::CheckoutOwner::kExternal);
  if (!first) {
    FAIL() << "Expected budgeted publication";
  }
  EXPECT_FALSE(cache.Store(2, std::make_shared<CachedNumber>(2)));
  const auto stats = cache.SnapshotStats();
  EXPECT_EQ(stats.budget, 1U);
  EXPECT_EQ(stats.consumed, 1U);
  EXPECT_EQ(stats.checked_out_external, 1U);
  EXPECT_EQ(stats.checked_out_internal, 0U);
  EXPECT_EQ(stats.pinned_external, 0U);
  EXPECT_EQ(stats.total_checkouts, 2U);
  EXPECT_FALSE(stats.over_budget);
  EXPECT_TRUE(cache.Release(std::move(first->ticket)));
  EXPECT_TRUE(cache.Remove(1));
  EXPECT_EQ(cache.Consumed(), 0U);
}

struct ImmediateIdleEviction : oxygen::RefCountedEviction<int> {
  using RefCountedEviction::RefCountedEviction;
  auto CheckIn(IteratorType& entry) -> std::optional<EntryType>
  {
    if (auto retired = RefCountedEviction::CheckIn(entry)) {
      return retired;
    }
    return RefCountOf(entry) == 1U ? Evict(entry) : std::nullopt;
  }
};

NOLINT_TEST_F(AnyCacheBasicTest, ReleaseHonorsImmediateEvictionPolicies)
{
  AnyCache<int, ImmediateIdleEviction> cache;
  auto acquired = cache.StoreAndAcquire(
    1, std::make_shared<CachedNumber>(1), oxygen::CheckoutOwner::kExternal);
  if (!acquired) {
    FAIL() << "Expected a publication";
  }
  const auto stamp = acquired->ticket.Entry();
  unsigned int calls = 0;
  auto scope = cache.OnEviction([&](const auto& retired) {
    ++calls;
    EXPECT_EQ(retired.entry, stamp);
    EXPECT_FALSE(cache.Contains(1));
  });
  EXPECT_TRUE(cache.Release(std::move(acquired->ticket)));
  EXPECT_EQ(calls, 1U);
  EXPECT_EQ(cache.Size(), 0U);
  EXPECT_EQ(acquired->value->value, 1);
}

// NOLINTEND(*-magic-numbers)
} // namespace
