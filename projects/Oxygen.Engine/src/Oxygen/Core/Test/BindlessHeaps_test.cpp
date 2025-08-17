//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <nlohmann/json.hpp>

#include <Oxygen/Core/Bindless/Generated.Heaps.D3D12.h>

using nlohmann::json;

namespace {

//! Parse the embedded D3D12 heap strategy JSON and verify required fields.
NOLINT_TEST(BindlessHeapsTest, ParseEmbeddedJson_VerifyMetaAndHeaps)
{
  // Arrange
  const char* json_text = oxygen::engine::binding::kD3D12HeapStrategyJson;

  // Act
  json parsed;
  ASSERT_NO_THROW(parsed = json::parse(json_text))
    << "Failed to parse embedded D3D12 strategy JSON";

  // Assert
  ASSERT_TRUE(parsed.is_object());

  // $meta must exist and contain expected fields
  ASSERT_TRUE(parsed.contains("$meta"));
  const auto& meta = parsed["$meta"];
  ASSERT_TRUE(meta.is_object());
  ASSERT_TRUE(meta.contains("format"));
  EXPECT_EQ(meta["format"], "D3D12HeapStrategy/2");

  // Required entries should exist under top-level 'heaps'
  ASSERT_TRUE(parsed.contains("heaps"));
  const auto& heaps = parsed["heaps"];
  ASSERT_TRUE(heaps.is_object());
  EXPECT_TRUE(heaps.contains("CBV_SRV_UAV:cpu"));
  EXPECT_TRUE(heaps.contains("CBV_SRV_UAV:gpu"));
  EXPECT_TRUE(heaps.contains("SAMPLER:cpu"));
  EXPECT_TRUE(heaps.contains("SAMPLER:gpu"));

  // Validate structure and invariants for all heap entries
  for (auto it = heaps.begin(); it != heaps.end(); ++it) {
    const std::string key = it.key();

    const auto& v = it.value();
    ASSERT_TRUE(v.is_object()) << "Entry '" << key << "' must be an object";

    // Mandatory fields
    for (const char* field :
      { "cpu_visible_capacity", "shader_visible_capacity", "allow_growth",
        "growth_factor", "max_growth_iterations", "base_index" }) {
      EXPECT_TRUE(v.contains(field))
        << "Entry '" << key << "' missing field '" << field << "'";
    }

    if (!v.contains("cpu_visible_capacity")
      || !v.contains("shader_visible_capacity") || !v.contains("allow_growth")
      || !v.contains("growth_factor") || !v.contains("max_growth_iterations")
      || !v.contains("base_index")) {
      continue; // Avoid cascading failures
    }

    // Basic type/range checks
    EXPECT_GE(v["cpu_visible_capacity"].get<int64_t>(), 0) << key;
    EXPECT_GE(v["shader_visible_capacity"].get<int64_t>(), 0) << key;
    EXPECT_GE(v["base_index"].get<int64_t>(), 0) << key;
    EXPECT_GE(v["max_growth_iterations"].get<int64_t>(), 0) << key;
    EXPECT_TRUE(v["allow_growth"].is_boolean()) << key;
    EXPECT_TRUE(v["growth_factor"].is_number()) << key;

    // Visibility-specific capacity constraints
    const auto colon = key.find(':');
    ASSERT_NE(colon, std::string::npos)
      << "Entry key must include visibility: '" << key << "'";
    const std::string visibility = key.substr(colon + 1);
    if (visibility == "cpu") {
      EXPECT_GT(v["cpu_visible_capacity"].get<int64_t>(), 0) << key;
      EXPECT_EQ(v["shader_visible_capacity"].get<int64_t>(), 0) << key;
    } else if (visibility == "gpu") {
      EXPECT_EQ(v["cpu_visible_capacity"].get<int64_t>(), 0) << key;
      EXPECT_GT(v["shader_visible_capacity"].get<int64_t>(), 0) << key;
    } else {
      ADD_FAILURE() << "Unknown visibility for entry '" << key << "'";
    }
  }
}

} // namespace
