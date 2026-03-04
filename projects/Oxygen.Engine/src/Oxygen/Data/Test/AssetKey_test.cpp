//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Standard library
#include <algorithm>
#include <array>
#include <string_view>
#include <unordered_set>
#include <vector>

// GTest
#include <Oxygen/Testing/GTest.h>

// Project
#include <Oxygen/Data/AssetKey.h>

using oxygen::data::AssetKey;
using oxygen::data::to_string;

namespace {

//! Basic tests for deterministic AssetKey generation from virtual paths.
class AssetKeyBasicTest : public testing::Test { };

NOLINT_TEST_F(AssetKeyBasicTest, CanonicalVirtualPathDeterministic)
{
  constexpr auto kPath
    = std::string_view("/Game/Physics/Materials/Rubber.opmat");

  const auto key0 = AssetKey::FromVirtualPath(kPath);
  const auto key1 = AssetKey::FromVirtualPath(kPath);

  EXPECT_EQ(key0, key1);
  EXPECT_EQ(to_string(key0), to_string(key1));
  EXPECT_EQ(std::hash<AssetKey> {}(key0), std::hash<AssetKey> {}(key1));
}

//! Tests different canonical paths map to different keys.
class AssetKeyOrderingTest : public testing::Test { };

NOLINT_TEST_F(AssetKeyOrderingTest, DifferentCanonicalPathsProduceDistinctKeys)
{
  constexpr auto kPaths = std::array<std::string_view, 6> {
    "/Game/Physics/Materials/Rubber.opmat",
    "/Game/Physics/Materials/Ice.opmat",
    "/Game/Physics/Shapes/BoulderConvexHull.ocshape",
    "/Engine/Physics/Materials/Default.opmat",
    "/Pak/DLC01/Game/Physics/Materials/Lava.opmat",
    "/.cooked/Physics/Materials/Rubber.opmat",
  };

  auto keys = std::vector<AssetKey> {};
  keys.reserve(kPaths.size());
  for (const auto path : kPaths) {
    keys.push_back(AssetKey::FromVirtualPath(path));
  }

  auto unique_keys = std::unordered_set<AssetKey> {};
  auto unique_hashes = std::unordered_set<size_t> {};
  auto unique_text = std::unordered_set<std::string> {};
  for (const auto& key : keys) {
    unique_keys.insert(key);
    unique_hashes.insert(std::hash<AssetKey> {}(key));
    unique_text.insert(to_string(key));
  }

  EXPECT_EQ(unique_keys.size(), keys.size());
  EXPECT_EQ(unique_hashes.size(), keys.size());
  EXPECT_EQ(unique_text.size(), keys.size());
}

} // namespace
