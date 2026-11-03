//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include <Oxygen/Cooker/testing/DescriptorDefects.h>
#include <Oxygen/Cooker/testing/ProductRefusal.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace data = oxygen::data;
namespace script = data::pak::scripting;
using data::CookedDefect;
using oxygen::cooker::testing::DescriptorDefects;
using oxygen::cooker::testing::SceneBuilder;
using oxygen::cooker::testing::SceneRefusal;
using oxygen::cooker::testing::ScriptParam;

constexpr uint32_t kSlotCount = 2U;

//! A scene of one node with two script slots, each with the int32 parameter
//! "v" holding the slot's position.
auto ValidScene() -> std::vector<std::byte>
{
  auto scene = SceneBuilder { "SceneScriptLayoutTest" };
  const auto root = scene.Node("root");
  for (auto i = uint32_t { 0 }; i < kSlotCount; ++i) {
    scene.ScriptSlot(root,
      data::AssetKey::FromVirtualPath("/test/script.oscript"),
      { ScriptParam { .key = "v", .value = static_cast<int32_t>(i) } });
  }
  return scene.Bytes();
}

NOLINT_TEST(SceneScriptLayoutTest, ReadsLocalSlotsAndParameters)
{
  const auto bytes = ValidScene();
  const auto scene = data::SceneAsset({}, bytes);
  EXPECT_TRUE(scene.ReadScriptSlots(0U, 0U).empty());
  const auto slots = scene.ReadScriptSlots(0U, kSlotCount);
  ASSERT_EQ(slots.size(), kSlotCount);
  for (size_t i = 0; i < slots.size(); ++i) {
    const auto parameters = scene.ReadScriptParameters(slots.at(i));
    ASSERT_EQ(parameters.size(), 1U);
    EXPECT_EQ(parameters.front().value.as_int32, static_cast<int32_t>(i));
  }
  EXPECT_THROW(static_cast<void>(scene.ReadScriptSlots(kSlotCount, 1U)),
    std::out_of_range);
  auto invalid = slots.front();
  invalid.params_array_offset = std::numeric_limits<uint64_t>::max();
  EXPECT_THROW(
    static_cast<void>(scene.ReadScriptParameters(invalid)), std::out_of_range);
}

NOLINT_TEST(SceneScriptLayoutTest, RejectsOverlappingParameterRanges)
{
  auto bytes = ValidScene();
  auto slot = DescriptorDefects::ScriptSlotAt(bytes, 1U);
  slot.params_array_offset
    = DescriptorDefects::ScriptSlotAt(bytes, 0U).params_array_offset;
  DescriptorDefects::SetScriptSlot(bytes, 1U, slot);

  EXPECT_EQ(SceneRefusal(bytes), CookedDefect::kOverlap);
}

NOLINT_TEST(SceneScriptLayoutTest, RejectsTruncatedParameters)
{
  auto bytes = ValidScene();
  bytes.resize(static_cast<size_t>(
    DescriptorDefects::ScriptSlotAt(bytes, 1U).params_array_offset));

  EXPECT_EQ(SceneRefusal(bytes), CookedDefect::kOutOfBounds);
}

NOLINT_TEST(SceneScriptLayoutTest, RejectsUnownedSlots)
{
  auto bytes = ValidScene();
  auto component
    = DescriptorDefects::ComponentAt<script::ScriptingComponentRecord>(
      bytes, data::ComponentType::kScripting, 0U);
  component.slot_count = 1U;
  DescriptorDefects::SetComponent(
    bytes, data::ComponentType::kScripting, 0U, component);

  EXPECT_EQ(SceneRefusal(bytes), CookedDefect::kOverlap);
}

NOLINT_TEST(SceneScriptLayoutTest, RejectsInvalidLocalSlotRange)
{
  auto bytes = ValidScene();
  auto component
    = DescriptorDefects::ComponentAt<script::ScriptingComponentRecord>(
      bytes, data::ComponentType::kScripting, 0U);
  component.slot_start_index = std::numeric_limits<uint32_t>::max();
  DescriptorDefects::SetComponent(
    bytes, data::ComponentType::kScripting, 0U, component);

  EXPECT_EQ(SceneRefusal(bytes), CookedDefect::kOutOfBounds);
}

NOLINT_TEST(SceneScriptLayoutTest, RejectsMissingScriptIdentity)
{
  auto bytes = ValidScene();
  auto slot = DescriptorDefects::ScriptSlotAt(bytes, 0U);
  slot.script_asset_key = {};
  DescriptorDefects::SetScriptSlot(bytes, 0U, slot);

  EXPECT_EQ(SceneRefusal(bytes), CookedDefect::kMissingPart);
}

//! A change of a slot's parameter names the slot by its ID, so every slot has
//! one, unique among its node's slots.
NOLINT_TEST(SceneScriptLayoutTest, RejectsMissingOrRepeatedSlotIds)
{
  const auto valid = ValidScene();
  auto missing = valid;
  auto slot = DescriptorDefects::ScriptSlotAt(valid, 0U);
  slot.slot_id = {};
  DescriptorDefects::SetScriptSlot(missing, 0U, slot);
  auto repeated = valid;
  slot.slot_id = DescriptorDefects::ScriptSlotAt(valid, 1U).slot_id;
  DescriptorDefects::SetScriptSlot(repeated, 0U, slot);

  EXPECT_EQ(SceneRefusal(missing), CookedDefect::kMissingPart);
  EXPECT_EQ(SceneRefusal(repeated), CookedDefect::kDuplicate);
}
} // namespace
