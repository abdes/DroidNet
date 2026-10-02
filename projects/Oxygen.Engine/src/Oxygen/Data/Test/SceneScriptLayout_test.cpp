//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace data = oxygen::data;
namespace world = data::pak::world;
namespace script = data::pak::scripting;

class SceneScriptLayoutTest : public testing::Test {
protected:
  SceneScriptLayoutTest()
  {
    descriptor_.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kScene);
    descriptor_.header.version = world::kSceneAssetVersion;
    descriptor_.nodes = {
      .offset = sizeof(descriptor_), .count = 1U, .entry_size = sizeof(node_)
    };
    descriptor_.scene_strings.offset = sizeof(descriptor_) + sizeof(node_);
    descriptor_.scene_strings.size = 1U;
    descriptor_.component_table_directory_offset
      = descriptor_.scene_strings.offset + 1U;
    descriptor_.component_table_count = 1U;
    table_.component_type
      = static_cast<uint32_t>(data::ComponentType::kScripting);
    table_.table = { .offset
      = descriptor_.component_table_directory_offset + sizeof(table_),
      .count = 1U,
      .entry_size = sizeof(component_) };
    component_.slot_count = 2U;
    node_.node_id = data::AssetKey::FromVirtualPath("/Test/root");
    node_.parent_index = 0U;
    descriptor_.script_slots
      = { .offset = table_.table.offset + sizeof(component_),
          .count = 2U,
          .entry_size = sizeof(script::ScriptSlotRecord) };
    auto offset = descriptor_.script_slots.offset + sizeof(slots_);
    for (size_t i = 0; i < slots_.size(); ++i) {
      auto& slot = slots_.at(i);
      slot.script_asset_key
        = data::AssetKey::FromVirtualPath("/Test/script.oscript");
      slot.params_count = 1U;
      slot.params_array_offset = offset;
      offset += sizeof(script::ScriptParamRecord);
      auto& parameter = parameters_.at(i);
      parameter.key[0] = 'v';
      parameter.type = script::ScriptParamType::kInt32;
      parameter.value.as_int32 = static_cast<int32_t>(i);
    }
    environment_.byte_size = sizeof(environment_);
  }

  auto Bytes() const -> std::vector<std::byte>
  {
    std::vector<std::byte> result;
    const auto append = [&result](const auto& value) {
      const auto bytes = std::as_bytes(std::span(&value, 1U));
      result.insert(result.end(), bytes.begin(), bytes.end());
    };
    append(descriptor_);
    append(node_);
    result.push_back(std::byte { 0 });
    append(table_);
    append(component_);
    append(slots_);
    append(parameters_);
    append(environment_);
    return result;
  }

  world::SceneAssetDesc descriptor_ {};
  world::NodeRecord node_ {};
  world::SceneComponentTableDesc table_ {};
  script::ScriptingComponentRecord component_ {};
  std::array<script::ScriptSlotRecord, 2> slots_ {};
  std::array<script::ScriptParamRecord, 2> parameters_ {};
  world::SceneEnvironmentBlockHeader environment_ {};
};

NOLINT_TEST_F(SceneScriptLayoutTest, ReadsLocalSlotsAndParameters)
{
  const auto bytes = Bytes();
  const auto scene = data::SceneAsset({}, bytes);
  EXPECT_TRUE(scene.ReadScriptSlots(0U, 0U).empty());
  const auto slots = scene.ReadScriptSlots(0U, 2U);
  ASSERT_EQ(slots.size(), 2U);
  for (size_t i = 0; i < slots.size(); ++i) {
    const auto parameters = scene.ReadScriptParameters(slots.at(i));
    ASSERT_EQ(parameters.size(), 1U);
    EXPECT_EQ(parameters.front().value.as_int32, static_cast<int32_t>(i));
  }
  EXPECT_THROW(
    static_cast<void>(scene.ReadScriptSlots(2U, 1U)), std::out_of_range);
  auto invalid = slots.front();
  invalid.params_array_offset = std::numeric_limits<uint64_t>::max();
  EXPECT_THROW(
    static_cast<void>(scene.ReadScriptParameters(invalid)), std::out_of_range);
}

NOLINT_TEST_F(SceneScriptLayoutTest, RejectsOverlappingParameterRanges)
{
  slots_.back().params_array_offset = slots_.front().params_array_offset;
  const auto bytes = Bytes();
  EXPECT_THROW(
    static_cast<void>(data::SceneAsset({}, bytes)), std::runtime_error);
}

NOLINT_TEST_F(SceneScriptLayoutTest, RejectsTruncatedParameters)
{
  auto bytes = Bytes();
  bytes.resize(static_cast<size_t>(slots_.back().params_array_offset));
  EXPECT_THROW(
    static_cast<void>(data::SceneAsset({}, bytes)), std::runtime_error);
}

NOLINT_TEST_F(SceneScriptLayoutTest, RejectsUnownedSlots)
{
  component_.slot_count = 1U;
  const auto bytes = Bytes();
  EXPECT_THROW(
    static_cast<void>(data::SceneAsset({}, bytes)), std::runtime_error);
}

NOLINT_TEST_F(SceneScriptLayoutTest, RejectsInvalidLocalSlotRange)
{
  component_.slot_start_index = std::numeric_limits<uint32_t>::max();
  const auto bytes = Bytes();
  EXPECT_THROW(
    static_cast<void>(data::SceneAsset({}, bytes)), std::runtime_error);
}

NOLINT_TEST_F(SceneScriptLayoutTest, RejectsMissingScriptIdentity)
{
  slots_.front().script_asset_key = {};
  const auto bytes = Bytes();
  EXPECT_THROW(
    static_cast<void>(data::SceneAsset({}, bytes)), std::runtime_error);
}
} // namespace
