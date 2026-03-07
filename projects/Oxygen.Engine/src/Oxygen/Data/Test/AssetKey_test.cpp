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

NOLINT_TEST_F(AssetKeyBasicTest, CanonicalVirtualPathGoldenVectorsAreStable)
{
  struct Case final {
    std::string_view path;
    std::string_view expected_key_text;
  };

  constexpr auto kCases = std::array<Case, 6> {
    Case { "/Game/Physics/Materials/Rubber.opmat",
      "5793612a-1c25-ca81-a7a2-8e696378559e" },
    Case { "/Game/Physics/Materials/Ice.opmat",
      "2ae46e8e-adec-053c-79a8-4285244f4aa8" },
    Case { "/Game/Physics/Shapes/BoulderConvexHull.ocshape",
      "289f608f-101f-5a5d-b773-511a2a6f58f7" },
    Case { "/.cooked/Physics/Materials/Rubber.opmat",
      "ce5e7e39-2579-0900-0498-db0169cef835" },
    Case { "/.cooked/Scenes/physics_domains.opscene",
      "a88a8d99-89e0-172b-a204-efa6f602182a" },
    Case { "/Engine/Physics/Materials/Default.opmat",
      "cdd33a69-2a2d-8cd7-dbb9-3e626bdb26c3" },
  };

  for (const auto& test_case : kCases) {
    const auto key = AssetKey::FromVirtualPath(test_case.path);
    EXPECT_EQ(to_string(key), test_case.expected_key_text)
      << "path='" << test_case.path << "'";
  }
}

NOLINT_TEST_F(AssetKeyBasicTest, FromStringParsesCanonicalLowercaseText)
{
  constexpr auto kText
    = std::string_view { "5793612a-1c25-ca81-a7a2-8e696378559e" };

  const auto parsed = AssetKey::FromString(kText);

  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(to_string(parsed.value()), kText);
}

NOLINT_TEST_F(AssetKeyBasicTest, FromStringRejectsUppercaseText)
{
  constexpr auto kText
    = std::string_view { "5793612A-1C25-CA81-A7A2-8E696378559E" };

  const auto parsed = AssetKey::FromString(kText);

  EXPECT_FALSE(parsed.has_value());
}

} // namespace
