//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Standard library
#include <unordered_set>

// GTest
#include <Oxygen/Testing/GTest.h>

// Project
#include <Oxygen/Data/AssetKey.h>

using oxygen::data::AssetKey;
using oxygen::data::GenerateAssetGuid;
using oxygen::data::to_string;

namespace {
//! Basic tests for AssetKey uniqueness, string and hash stability.
class AssetKeyBasicTest : public testing::Test { };

NOLINT_TEST_F(AssetKeyBasicTest, GenerateDistinct_StableStringHash)
{
  // Arrange
  constexpr int kKeyCount = 32; // Enough to reduce collision probability
  std::vector<AssetKey> keys;
  keys.reserve(kKeyCount);
  std::unordered_set<std::string> string_reprs; // uniqueness by string
  std::unordered_set<size_t> hash_values; // uniqueness by hash
  std::unordered_set<AssetKey> key_set; // relies on operator<=> + hash

  // Act
  for (int i = 0; i < kKeyCount; ++i) {
    AssetKey key { .guid = GenerateAssetGuid() };
    keys.push_back(key);
    auto s = to_string(key);

    // Basic shape: 36 chars (8-4-4-4-12) with hyphens at fixed positions.
    ASSERT_EQ(s.size(), 36u) << "UUID string should be 36 chars";
    ASSERT_EQ(s[8], '-') << "Hyphen at pos 8";
    ASSERT_EQ(s[13], '-') << "Hyphen at pos 13";
    ASSERT_EQ(s[18], '-') << "Hyphen at pos 18";
    ASSERT_EQ(s[23], '-') << "Hyphen at pos 23";

    string_reprs.insert(s);
    hash_values.insert(std::hash<AssetKey> {}(key));
    key_set.insert(key);
  }

  // Assert
  EXPECT_EQ(string_reprs.size(), keys.size())
    << "All generated keys should have distinct string representations.";
  EXPECT_EQ(hash_values.size(), keys.size())
    << "All generated keys should have distinct hash values (very low "
       "collision probability).";
  EXPECT_EQ(key_set.size(), keys.size())
    << "All generated keys should be distinct as values.";

  // Stability: recompute string + hash for same keys and ensure unchanged.
  for (const auto& key : keys) {
    auto s1 = to_string(key);
    auto h1 = std::hash<AssetKey> {}(key);
    auto s2 = to_string(key);
    auto h2 = std::hash<AssetKey> {}(key);
    EXPECT_EQ(s1, s2) << "to_string must be deterministic.";
    EXPECT_EQ(h1, h2) << "hash must be deterministic.";
  }
}

//! Tests that AssetKey ordering matches lexical order of their string
//! representations.
class AssetKeyOrderingTest : public testing::Test { };

NOLINT_TEST_F(AssetKeyOrderingTest, LexicalOrderConsistentWithGuid)
{
  // Arrange
  constexpr int kKeyCount = 32;
  std::vector<AssetKey> keys;
  keys.reserve(kKeyCount);
  for (int i = 0; i < kKeyCount; ++i) {
    keys.push_back(AssetKey { .guid = GenerateAssetGuid() });
  }

  // Act: sort by AssetKey ordering
  std::vector<AssetKey> sorted_keys = keys;
  std::sort(sorted_keys.begin(), sorted_keys.end());

  // Get string representations in sorted order
  std::vector<std::string> sorted_strings;
  for (const auto& k : sorted_keys) {
    sorted_strings.push_back(to_string(k));
  }

  // Copy and sort the string representations lexicographically
  std::vector<std::string> lex_sorted = sorted_strings;
  std::sort(lex_sorted.begin(), lex_sorted.end());

  // Assert: the order of string representations matches the order of sorted
  // AssetKeys
  EXPECT_EQ(sorted_strings, lex_sorted)
    << "AssetKey ordering must match lexical order of to_string "
       "representation.";
}

} // namespace
