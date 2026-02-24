//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/LruEviction.h>
#include <Oxygen/Engine/Scripting/ScriptBytecodeBlob.h>

namespace {

using oxygen::LruEviction;
using oxygen::PolicyBudgetStatus;
using oxygen::TypeId;
using oxygen::scripting::ScriptBlobCanonicalName;
using oxygen::scripting::ScriptBlobOrigin;
using oxygen::scripting::ScriptBytecodeBlob;

constexpr uint8_t kFillPattern = 0xAB;
constexpr size_t kBudget = 128;

auto MakeBlob(const size_t size) -> std::shared_ptr<ScriptBytecodeBlob>
{
  std::vector<uint8_t> bytes(size, kFillPattern);
  return std::make_shared<ScriptBytecodeBlob>(ScriptBytecodeBlob::FromOwned(
    std::move(bytes), oxygen::data::pak::ScriptLanguage::kLuau,
    oxygen::data::pak::ScriptCompression::kNone, 0,
    ScriptBlobOrigin::kEmbeddedResource,
    ScriptBlobCanonicalName { "test-bytecode" }));
}

auto CreatePolicy(const size_t budget) -> LruEviction<uint64_t>
{
  LruEviction<uint64_t> policy(budget);
  policy.SetCostFunction(
    [](const std::shared_ptr<void>& value, const TypeId type_id) -> size_t {
      if (!value || type_id != ScriptBytecodeBlob::ClassTypeId()) {
        return 1;
      }
      return std::static_pointer_cast<const ScriptBytecodeBlob>(value)->Size();
    });
  return policy;
}

TEST(ScriptingLruEviction, CostUsesConfiguredFunction)
{
  auto policy = CreatePolicy(kBudget);
  const auto blob = MakeBlob(64);
  EXPECT_EQ(policy.Cost(std::static_pointer_cast<void>(blob),
              ScriptBytecodeBlob::ClassTypeId()),
    64U);
}

TEST(ScriptingLruEviction, StoreAndCheckInEvictsAtRefcountZero)
{
  auto policy = CreatePolicy(kBudget);
  auto it = policy.Store(policy.MakeEntry(7, ScriptBytecodeBlob::ClassTypeId(),
    std::static_pointer_cast<void>(MakeBlob(16))));
  ASSERT_FALSE(policy.IsEnd(it));
  EXPECT_EQ(policy.RefCountOf(it), 1U);
  EXPECT_EQ(policy.Consumed(), 16U);

  const auto evicted = policy.CheckIn(it);
  ASSERT_TRUE(evicted.has_value());
  EXPECT_EQ(policy.EntryKey(*evicted), 7U);
  EXPECT_EQ(policy.Consumed(), 0U);
}

TEST(ScriptingLruEviction, TryReplaceUpdatesConsumed)
{
  auto policy = CreatePolicy(kBudget);
  auto it = policy.Store(policy.MakeEntry(1, ScriptBytecodeBlob::ClassTypeId(),
    std::static_pointer_cast<void>(MakeBlob(10))));
  ASSERT_FALSE(policy.IsEnd(it));
  EXPECT_EQ(policy.Consumed(), 10U);

  EXPECT_TRUE(
    policy.TryReplace(it, std::static_pointer_cast<void>(MakeBlob(30)),
      ScriptBytecodeBlob::ClassTypeId()));
  EXPECT_EQ(policy.Consumed(), 30U);
}

TEST(ScriptingLruEviction, TryReplaceFailsWhenOverBudget)
{
  auto policy = CreatePolicy(20);
  auto it = policy.Store(policy.MakeEntry(1, ScriptBytecodeBlob::ClassTypeId(),
    std::static_pointer_cast<void>(MakeBlob(10))));
  ASSERT_FALSE(policy.IsEnd(it));

  EXPECT_FALSE(
    policy.TryReplace(it, std::static_pointer_cast<void>(MakeBlob(30)),
      ScriptBytecodeBlob::ClassTypeId()));
}

TEST(ScriptingLruEviction, EnforceBudgetEvictsFromLeastRecent)
{
  auto policy = CreatePolicy(64);
  auto it1 = policy.Store(policy.MakeEntry(1, ScriptBytecodeBlob::ClassTypeId(),
    std::static_pointer_cast<void>(MakeBlob(16))));
  auto it2 = policy.Store(policy.MakeEntry(2, ScriptBytecodeBlob::ClassTypeId(),
    std::static_pointer_cast<void>(MakeBlob(16))));
  auto it3 = policy.Store(policy.MakeEntry(3, ScriptBytecodeBlob::ClassTypeId(),
    std::static_pointer_cast<void>(MakeBlob(16))));
  ASSERT_FALSE(policy.IsEnd(it1));
  ASSERT_FALSE(policy.IsEnd(it2));
  ASSERT_FALSE(policy.IsEnd(it3));

  policy.SetBudget(16);
  std::vector<uint64_t> evicted;
  const auto status = policy.EnforceBudget(
    [&evicted](const uint64_t key) { evicted.push_back(key); });

  EXPECT_EQ(status, PolicyBudgetStatus::kSatisfied);
  EXPECT_EQ(policy.Consumed(), 16U);
  EXPECT_EQ(evicted.size(), 2U);
}

TEST(ScriptingLruEviction, EnforceBudgetBestEffortWhenAllEntriesPinned)
{
  auto policy = CreatePolicy(64);
  auto it1 = policy.Store(policy.MakeEntry(1, ScriptBytecodeBlob::ClassTypeId(),
    std::static_pointer_cast<void>(MakeBlob(16))));
  auto it2 = policy.Store(policy.MakeEntry(2, ScriptBytecodeBlob::ClassTypeId(),
    std::static_pointer_cast<void>(MakeBlob(16))));
  ASSERT_FALSE(policy.IsEnd(it1));
  ASSERT_FALSE(policy.IsEnd(it2));

  policy.CheckOut(it1);
  policy.CheckOut(it2);
  policy.SetBudget(1);

  std::vector<uint64_t> evicted;
  const auto status = policy.EnforceBudget(
    [&evicted](const uint64_t key) { evicted.push_back(key); });

  EXPECT_EQ(status, PolicyBudgetStatus::kUnsatisfiedBestEffort);
  EXPECT_TRUE(evicted.empty());
}

} // namespace
