//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include <Oxygen/Composition/Object.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/Internal/DependencyGraphStore.h>
#include <Oxygen/Content/Internal/DependencyReleaseEngine.h>
#include <Oxygen/Core/AnyCache.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::internal::DependencyGraphStore;
using oxygen::content::internal::DependencyReleaseEngine;

class CachedAsset final : public oxygen::Object {
  OXYGEN_TYPED(CachedAsset)
};

TEST(DependencyReleaseEngineTest, ReleaseCannotConsumeAnotherGenerationEdges)
{
  constexpr uint64_t kOldRoot = 1U;
  constexpr uint64_t kNewRoot = 2U;
  constexpr uint64_t kOldMaterial = 3U;
  constexpr uint64_t kNewMaterial = 4U;
  DependencyReleaseEngine::CacheT cache(8U);
  DependencyGraphStore graph;
  for (const auto key : { kOldRoot, kNewRoot, kOldMaterial, kNewMaterial }) {
    ASSERT_TRUE(cache.Store(key, std::make_shared<CachedAsset>()));
  }
  ASSERT_TRUE(cache.Pin(kOldMaterial, oxygen::CheckoutOwner::kInternal));
  ASSERT_TRUE(cache.Pin(kNewMaterial, oxygen::CheckoutOwner::kInternal));
  ASSERT_TRUE(graph.AddAssetDependency(kOldRoot, kOldMaterial));
  ASSERT_TRUE(graph.AddAssetDependency(kNewRoot, kNewMaterial));

  DependencyReleaseEngine engine;
  engine.ReleaseAssetTree(kOldRoot, graph, cache);

  EXPECT_FALSE(cache.Contains(kOldRoot));
  EXPECT_EQ(cache.GetCheckoutCount(kOldMaterial), 1U);
  EXPECT_EQ(cache.GetCheckoutCount(kNewMaterial), 2U);
  EXPECT_TRUE(cache.Contains(kNewRoot));
  EXPECT_EQ(graph.FindAssetDependencies(kOldRoot), nullptr);
  const auto* remaining = graph.FindAssetDependencies(kNewRoot);
  ASSERT_NE(remaining, nullptr);
  EXPECT_TRUE(remaining->contains(kNewMaterial));
}

TEST(DependencyReleaseEngineTest, TrimKeepsSeparateRootsForTheSameAssetKey)
{
  constexpr uint64_t kOldRoot = 1U;
  constexpr uint64_t kNewRoot = 2U;
  constexpr uint64_t kOldMaterial = 3U;
  constexpr uint64_t kNewMaterial = 4U;
  DependencyReleaseEngine::CacheT cache(8U);
  DependencyGraphStore graph;
  for (const auto key : { kOldRoot, kNewRoot, kOldMaterial, kNewMaterial }) {
    ASSERT_TRUE(cache.Store(key, std::make_shared<CachedAsset>()));
  }
  ASSERT_TRUE(cache.Pin(kNewRoot, oxygen::CheckoutOwner::kExternal));
  ASSERT_TRUE(cache.Pin(kOldMaterial, oxygen::CheckoutOwner::kInternal));
  ASSERT_TRUE(cache.Pin(kNewMaterial, oxygen::CheckoutOwner::kInternal));
  ASSERT_TRUE(graph.AddAssetDependency(kOldRoot, kOldMaterial));
  ASSERT_TRUE(graph.AddAssetDependency(kNewRoot, kNewMaterial));
  const std::array identities { kOldRoot, kNewRoot, kOldMaterial,
    kNewMaterial };

  DependencyReleaseEngine engine;
  const auto result = engine.TrimCache(identities, {}, graph, cache);

  EXPECT_EQ(result.trim_roots, 1U);
  EXPECT_FALSE(cache.Contains(kOldRoot));
  EXPECT_FALSE(cache.Contains(kOldMaterial));
  EXPECT_EQ(cache.GetCheckoutCount(kNewRoot), 2U);
  EXPECT_EQ(cache.GetCheckoutCount(kNewMaterial), 2U);
  EXPECT_NE(graph.FindAssetDependencies(kNewRoot), nullptr);
}

TEST(DependencyReleaseEngineTest, SharedDependencyTrimsAfterItsLastParent)
{
  constexpr uint64_t kFirstRoot = 1U;
  constexpr uint64_t kSecondRoot = 2U;
  constexpr uint64_t kShared = 3U;
  constexpr uint64_t kLeaf = 4U;
  DependencyReleaseEngine::CacheT cache(8U);
  DependencyGraphStore graph;
  auto identities = std::vector<uint64_t> {};
  for (const auto key : { kFirstRoot, kSecondRoot, kShared, kLeaf }) {
    ASSERT_TRUE(cache.Store(key, std::make_shared<CachedAsset>()));
    identities.push_back(key);
  }
  ASSERT_TRUE(cache.Pin(kShared, oxygen::CheckoutOwner::kInternal));
  ASSERT_TRUE(cache.Pin(kShared, oxygen::CheckoutOwner::kInternal));
  ASSERT_TRUE(cache.Pin(kLeaf, oxygen::CheckoutOwner::kInternal));
  ASSERT_TRUE(graph.AddAssetDependency(kFirstRoot, kShared));
  ASSERT_TRUE(graph.AddAssetDependency(kSecondRoot, kShared));
  ASSERT_TRUE(graph.AddAssetDependency(kShared, kLeaf));

  DependencyReleaseEngine engine;
  const auto result = engine.TrimCache(identities, {}, graph, cache);

  EXPECT_EQ(result.trim_roots, 2U);
  EXPECT_FALSE(cache.Contains(kFirstRoot));
  EXPECT_FALSE(cache.Contains(kSecondRoot));
  EXPECT_FALSE(cache.Contains(kShared));
  EXPECT_FALSE(cache.Contains(kLeaf));
  EXPECT_TRUE(graph.AssetDependencies().empty());
}

} // namespace
