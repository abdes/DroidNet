//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Engine/Scripting/Detail/LruEviction.h>

namespace {

// Constants to avoid magic numbers
constexpr uint8_t kFillPattern = 0xAB;
constexpr size_t kDefaultBudget = 1024;
constexpr size_t kSmallBudget = 128;
constexpr size_t kTinyBudget = 40;
constexpr size_t kMicroBudget = 8;

// NOLINTBEGIN(*-magic-numbers)

using oxygen::kInvalidTypeId;
using oxygen::TypeId;
using oxygen::scripting::ScriptBlobCanonicalName;
using oxygen::scripting::ScriptBlobOrigin;
using oxygen::scripting::ScriptBytecodeBlob;
using oxygen::scripting::detail::LruEviction;

auto MakeBlob(const size_t size) -> std::shared_ptr<ScriptBytecodeBlob>
{
  std::vector<uint8_t> bytes(size, kFillPattern);
  return std::make_shared<ScriptBytecodeBlob>(ScriptBytecodeBlob::FromOwned(
    std::move(bytes), oxygen::data::pak::ScriptLanguage::kLuau,
    oxygen::data::pak::ScriptCompression::kNone, 0,
    ScriptBlobOrigin::kEmbeddedResource,
    ScriptBlobCanonicalName { "test-bytecode" }));
}

auto MakeEntry(const uint64_t key, std::shared_ptr<void> value,
  const TypeId type_id) -> LruEviction<uint64_t>::EntryType
{
  return std::make_tuple(key, type_id, std::move(value), 0U);
}

auto CreateEviction(const size_t budget) -> LruEviction<uint64_t>
{
  return LruEviction<uint64_t>(budget,
    [](const std::shared_ptr<void>& value, const TypeId type_id) -> size_t {
      if (!value) {
        return 1;
      }
      if (type_id == ScriptBytecodeBlob::ClassTypeId()) {
        const auto blob
          = std::static_pointer_cast<const ScriptBytecodeBlob>(value);
        return (std::max)(size_t(1), blob->Size());
      }
      return 1;
    });
}

TEST(ScriptingLruEviction, CostIsBlobSizeForScriptType)
{
  auto eviction = CreateEviction(kDefaultBudget);
  const auto blob = MakeBlob(64);
  const auto value = std::static_pointer_cast<void>(blob);
  EXPECT_EQ(eviction.Cost(value, ScriptBytecodeBlob::ClassTypeId()), 64U);
}

TEST(ScriptingLruEviction, CostIsOneForNullOrNonScriptType)
{
  auto eviction = CreateEviction(kDefaultBudget);
  EXPECT_EQ(eviction.Cost(nullptr, ScriptBytecodeBlob::ClassTypeId()), 1U);
  const auto blob = MakeBlob(64);
  const auto value = std::static_pointer_cast<void>(blob);
  EXPECT_EQ(eviction.Cost(value, kInvalidTypeId), 1U);
}

TEST(ScriptingLruEviction, StoreSetsRefcountAndTracksBudget)
{
  auto eviction = CreateEviction(kSmallBudget);
  auto entry = MakeEntry(7, std::static_pointer_cast<void>(MakeBlob(32)),
    ScriptBytecodeBlob::ClassTypeId());
  const auto it = eviction.Store(std::move(entry));
  ASSERT_NE(it, eviction.eviction_list.end());
  EXPECT_EQ(std::get<0>(*it), 7U);
  EXPECT_EQ(std::get<3>(*it), 1U);
  EXPECT_EQ(eviction.consumed, 32U);
}

TEST(ScriptingLruEviction, StoreRejectsItemWhenBudgetExceeded)
{
  auto eviction = CreateEviction(kMicroBudget);
  auto entry = MakeEntry(1, std::static_pointer_cast<void>(MakeBlob(16)),
    ScriptBytecodeBlob::ClassTypeId());
  const auto it = eviction.Store(std::move(entry));
  EXPECT_EQ(it, eviction.eviction_list.end());
  EXPECT_EQ(eviction.consumed, 0U);
  EXPECT_TRUE(eviction.eviction_list.empty());
}

TEST(ScriptingLruEviction, TryReplaceUpdatesConsumedAndMovesToFront)
{
  auto eviction = CreateEviction(kSmallBudget);
  auto first
    = eviction.Store(MakeEntry(1, std::static_pointer_cast<void>(MakeBlob(10)),
      ScriptBytecodeBlob::ClassTypeId()));
  auto second
    = eviction.Store(MakeEntry(2, std::static_pointer_cast<void>(MakeBlob(20)),
      ScriptBytecodeBlob::ClassTypeId()));
  ASSERT_NE(first, eviction.eviction_list.end());
  ASSERT_NE(second, eviction.eviction_list.end());
  EXPECT_EQ(eviction.consumed, 30U);

  const auto replaced
    = eviction.TryReplace(first, std::static_pointer_cast<void>(MakeBlob(30)));
  EXPECT_TRUE(replaced);
  EXPECT_EQ(eviction.consumed, 50U);
  EXPECT_EQ(std::get<0>(eviction.eviction_list.front()), 1U);
}

TEST(ScriptingLruEviction, TryReplaceFailsWhenCheckedOutByOthers)
{
  auto eviction = CreateEviction(kSmallBudget);
  auto it
    = eviction.Store(MakeEntry(1, std::static_pointer_cast<void>(MakeBlob(10)),
      ScriptBytecodeBlob::ClassTypeId()));
  ASSERT_NE(it, eviction.eviction_list.end());

  eviction.CheckOut(it);
  EXPECT_EQ(std::get<3>(*it), 2U);
  const auto replaced
    = eviction.TryReplace(it, std::static_pointer_cast<void>(MakeBlob(20)));
  EXPECT_FALSE(replaced);
}

TEST(ScriptingLruEviction, TryReplaceFailsWhenReplacementWouldExceedBudget)
{
  auto eviction = CreateEviction(kTinyBudget);
  auto it
    = eviction.Store(MakeEntry(1, std::static_pointer_cast<void>(MakeBlob(20)),
      ScriptBytecodeBlob::ClassTypeId()));
  ASSERT_NE(it, eviction.eviction_list.end());

  const auto replaced
    = eviction.TryReplace(it, std::static_pointer_cast<void>(MakeBlob(50)));
  EXPECT_FALSE(replaced);
  EXPECT_EQ(eviction.consumed, 20U);
}

TEST(ScriptingLruEviction, CheckOutAndCheckInWithoutEvictionPreservesEntry)
{
  auto eviction = CreateEviction(kSmallBudget);
  auto it
    = eviction.Store(MakeEntry(1, std::static_pointer_cast<void>(MakeBlob(10)),
      ScriptBytecodeBlob::ClassTypeId()));
  ASSERT_NE(it, eviction.eviction_list.end());

  eviction.CheckOut(it);
  EXPECT_EQ(std::get<3>(*it), 2U);
  const auto check_in = eviction.CheckIn(it);
  EXPECT_FALSE(check_in.has_value());
  EXPECT_EQ(std::get<3>(*it), 1U);
  EXPECT_EQ(eviction.consumed, 10U);
}

TEST(ScriptingLruEviction, CheckInEvictsWhenRefcountReachesZero)
{
  auto eviction = CreateEviction(kSmallBudget);
  auto it
    = eviction.Store(MakeEntry(9, std::static_pointer_cast<void>(MakeBlob(10)),
      ScriptBytecodeBlob::ClassTypeId()));
  ASSERT_NE(it, eviction.eviction_list.end());

  const auto evicted = eviction.CheckIn(it);
  ASSERT_TRUE(evicted.has_value());
  EXPECT_EQ(std::get<0>(*evicted), 9U);
  EXPECT_EQ(eviction.consumed, 0U);
  EXPECT_TRUE(eviction.eviction_list.empty());
}

TEST(ScriptingLruEviction, EvictRespectsRefcountGate)
{
  auto eviction = CreateEviction(kSmallBudget);
  auto it
    = eviction.Store(MakeEntry(3, std::static_pointer_cast<void>(MakeBlob(10)),
      ScriptBytecodeBlob::ClassTypeId()));
  ASSERT_NE(it, eviction.eviction_list.end());

  eviction.CheckOut(it);
  EXPECT_FALSE(eviction.Evict(it).has_value());
  EXPECT_EQ(eviction.consumed, 10U);

  const auto first_check_in = eviction.CheckIn(it);
  EXPECT_FALSE(first_check_in.has_value());
  const auto evicted = eviction.Evict(it);
  EXPECT_TRUE(evicted.has_value());
  EXPECT_EQ(eviction.consumed, 0U);
}

TEST(ScriptingLruEviction, FitEvictsFromBackUntilWithinBudget)
{
  auto eviction = CreateEviction(kTinyBudget);
  auto it1
    = eviction.Store(MakeEntry(1, std::static_pointer_cast<void>(MakeBlob(10)),
      ScriptBytecodeBlob::ClassTypeId()));
  auto it2
    = eviction.Store(MakeEntry(2, std::static_pointer_cast<void>(MakeBlob(10)),
      ScriptBytecodeBlob::ClassTypeId()));
  auto it3
    = eviction.Store(MakeEntry(3, std::static_pointer_cast<void>(MakeBlob(10)),
      ScriptBytecodeBlob::ClassTypeId()));
  ASSERT_NE(it1, eviction.eviction_list.end());
  ASSERT_NE(it2, eviction.eviction_list.end());
  ASSERT_NE(it3, eviction.eviction_list.end());
  EXPECT_EQ(eviction.consumed, 30U);

  eviction.budget = 15U;
  std::vector<uint64_t> evicted_keys;
  eviction.Fit(
    [&evicted_keys](const uint64_t key) { evicted_keys.push_back(key); });

  EXPECT_EQ(eviction.consumed, 10U);
  EXPECT_EQ(evicted_keys.size(), 2U);
  EXPECT_TRUE(std::ranges::find(evicted_keys, 1U) != evicted_keys.end());
  EXPECT_TRUE(std::ranges::find(evicted_keys, 2U) != evicted_keys.end());
}

TEST(ScriptingLruEviction, FitSkipsCheckedOutEntries)
{
  auto eviction = CreateEviction(kTinyBudget);
  auto it1
    = eviction.Store(MakeEntry(1, std::static_pointer_cast<void>(MakeBlob(10)),
      ScriptBytecodeBlob::ClassTypeId()));
  auto it2
    = eviction.Store(MakeEntry(2, std::static_pointer_cast<void>(MakeBlob(10)),
      ScriptBytecodeBlob::ClassTypeId()));
  ASSERT_NE(it1, eviction.eviction_list.end());
  ASSERT_NE(it2, eviction.eviction_list.end());

  eviction.CheckOut(it1);
  eviction.CheckOut(it2);
  eviction.budget = 1U;

  std::vector<uint64_t> evicted_keys;
  eviction.Fit(
    [&evicted_keys](const uint64_t key) { evicted_keys.push_back(key); });

  EXPECT_TRUE(evicted_keys.empty());
  EXPECT_EQ(eviction.consumed, 20U);
}

TEST(ScriptingLruEviction, ClearResetsState)
{
  auto eviction = CreateEviction(kDefaultBudget);
  auto it
    = eviction.Store(MakeEntry(99, std::static_pointer_cast<void>(MakeBlob(32)),
      ScriptBytecodeBlob::ClassTypeId()));
  ASSERT_NE(it, eviction.eviction_list.end());
  ASSERT_FALSE(eviction.eviction_list.empty());

  eviction.Clear();

  EXPECT_EQ(eviction.consumed, 0U);
  EXPECT_TRUE(eviction.eviction_list.empty());
}

TEST(ScriptingLruEviction, AccessPromotesToMru)
{
  auto eviction = CreateEviction(kTinyBudget);
  auto it_a
    = eviction.Store(MakeEntry(1, std::static_pointer_cast<void>(MakeBlob(10)),
      ScriptBytecodeBlob::ClassTypeId()));
  eviction.Store(MakeEntry(2, std::static_pointer_cast<void>(MakeBlob(10)),
    ScriptBytecodeBlob::ClassTypeId()));
  eviction.Store(MakeEntry(3, std::static_pointer_cast<void>(MakeBlob(10)),
    ScriptBytecodeBlob::ClassTypeId()));

  // Current Order (MRU->LRU): C, B, A.

  // Access A: CheckOut then CheckIn. Should move A to MRU.
  eviction.CheckOut(it_a);
  // Order: A, C, B
  auto check_in = eviction.CheckIn(it_a);
  EXPECT_FALSE(check_in.has_value());

  // Reduce budget to 20 (hold 2 items). Should evict 1 item (LRU = B).
  eviction.budget = 20U;

  std::vector<uint64_t> evicted_keys;
  eviction.Fit(
    [&evicted_keys](const uint64_t key) { evicted_keys.push_back(key); });

  ASSERT_EQ(evicted_keys.size(), 1U);
  EXPECT_EQ(evicted_keys[0], 2U); // B should be evicted.

  EXPECT_EQ(eviction.consumed, 20U);
  // Verify A is still there (key 1)
  EXPECT_EQ(std::get<0>(*it_a), 1U);
}

TEST(ScriptingLruEviction, FitEvictsNextAvailableLru)
{
  auto eviction = CreateEviction(kTinyBudget);
  auto it_a
    = eviction.Store(MakeEntry(1, std::static_pointer_cast<void>(MakeBlob(10)),
      ScriptBytecodeBlob::ClassTypeId()));
  eviction.Store(MakeEntry(2, std::static_pointer_cast<void>(MakeBlob(10)),
    ScriptBytecodeBlob::ClassTypeId()));
  eviction.Store(MakeEntry(3, std::static_pointer_cast<void>(MakeBlob(10)),
    ScriptBytecodeBlob::ClassTypeId()));

  // Current Order (MRU->LRU): C, B, A.

  // CheckOut A. It becomes protected (Refcount 2) and moves to MRU.
  // Order: A, C, B.
  eviction.CheckOut(it_a);

  // Reduce budget to 10. Needs to evict 2 items (20 units).
  eviction.budget = 10U;

  std::vector<uint64_t> evicted_keys;
  eviction.Fit(
    [&evicted_keys](const uint64_t key) { evicted_keys.push_back(key); });

  // fit() iterates from LRU (B).
  // B (Ref 1) -> Evict.
  // C (Ref 1) -> Evict.
  // A (Ref 2) -> Skip.

  ASSERT_EQ(evicted_keys.size(), 2U);

  // Verify contents
  bool evicted_b = std::ranges::find(evicted_keys, 2U) != evicted_keys.end();
  bool evicted_c = std::ranges::find(evicted_keys, 3U) != evicted_keys.end();

  EXPECT_TRUE(evicted_b);
  EXPECT_TRUE(evicted_c);

  // Verify A remains
  EXPECT_EQ(std::get<3>(*it_a), 2U);
  EXPECT_EQ(std::get<0>(*it_a), 1U);
}

// NOLINTEND(*-magic-numbers)

} // namespace
