//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdio>
#include <cstring>
#include <memory>
#include <span>

#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/Loaders/InputActionLoader.h>
#include <Oxygen/Content/Loaders/InputMappingContextLoader.h>
#include <Oxygen/Content/SourceToken.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/Writer.h>
#include <Oxygen/Testing/GTest.h>

#include "Mocks/MockStream.h"

using oxygen::serio::Reader;

namespace {

class InputLoadersTest : public testing::Test {
protected:
  using MockStream = oxygen::content::testing::MockStream;
  using Writer = oxygen::serio::Writer<MockStream>;

  InputLoadersTest()
    : writer_(stream_)
    , reader_(stream_)
  {
  }

  auto ResetForRead() -> void
  {
    if (!stream_.Seek(0)) {
      throw std::runtime_error("Failed to seek stream");
    }
  }

  auto MakeParseOnlyContext() -> oxygen::content::LoaderContext
  {
    ResetForRead();
    return oxygen::content::LoaderContext {
      .current_asset_key = oxygen::data::AssetKey {},
      .desc_reader = &reader_,
      .work_offline = true,
      .parse_only = true,
    };
  }

  auto MakeDecodeContext() -> std::pair<oxygen::content::LoaderContext,
    std::shared_ptr<oxygen::content::internal::DependencyCollector>>
  {
    ResetForRead();
    auto collector
      = std::make_shared<oxygen::content::internal::DependencyCollector>();
    return { oxygen::content::LoaderContext {
               .current_asset_key = oxygen::data::AssetKey {},
               .source_token = oxygen::content::internal::SourceToken(11U),
               .desc_reader = &reader_,
               .work_offline = true,
               .dependency_collector = collector,
               .parse_only = false,
             },
      collector };
  }

  auto WriteBlob(const auto& pod) -> void
  {
    auto bytes = std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(&pod), sizeof(pod));
    const auto res = writer_.WriteBlob(bytes);
    ASSERT_TRUE(res) << res.error().message();
  }

  MockStream stream_;
  Writer writer_;
  Reader<MockStream> reader_;
};

NOLINT_TEST_F(InputLoadersTest, LoadInputActionAssetValidDescriptorParses)
{
  using oxygen::content::loaders::LoadInputActionAsset;
  using oxygen::data::AssetType;
  using oxygen::data::pak::input::InputActionAssetDesc;
  using oxygen::data::pak::input::InputActionAssetFlags;

  InputActionAssetDesc desc {};
  desc.header.asset_type = static_cast<uint8_t>(AssetType::kInputAction);
  desc.header.version = oxygen::data::pak::input::kInputActionAssetVersion;
  std::snprintf(desc.header.name, sizeof(desc.header.name), "%s", "Accel");
  desc.value_type = 2;
  desc.flags = InputActionAssetFlags::kConsumesInput;

  WriteBlob(desc);
  ASSERT_TRUE(writer_.Flush());

  auto context = MakeParseOnlyContext();
  const auto asset = LoadInputActionAsset(context);
  ASSERT_THAT(asset, ::testing::NotNull());
  EXPECT_EQ(asset->GetValueTypeId(), 2);
  EXPECT_TRUE(asset->ConsumesInput());
}

NOLINT_TEST_F(InputLoadersTest,
  LoadInputMappingContextAssetDecodeCollectsActionDependencies)
{
  using oxygen::content::loaders::LoadInputMappingContextAsset;
  using oxygen::data::AssetKey;
  using oxygen::data::AssetType;
  using oxygen::data::pak::input::InputActionMappingRecord;
  using oxygen::data::pak::input::InputMappingContextAssetDesc;
  using oxygen::data::pak::input::InputTriggerAuxRecord;
  using oxygen::data::pak::input::InputTriggerBehavior;
  using oxygen::data::pak::input::InputTriggerRecord;
  using oxygen::data::pak::input::InputTriggerType;

  constexpr size_t kDescSize = sizeof(InputMappingContextAssetDesc);
  constexpr size_t kMappingSize = sizeof(InputActionMappingRecord);
  constexpr size_t kTriggerSize = sizeof(InputTriggerRecord);
  constexpr size_t kAuxSize = sizeof(InputTriggerAuxRecord);

  AssetKey action_a {};
  action_a.guid[0] = 0xAA;
  AssetKey action_b {};
  action_b.guid[0] = 0xBB;
  AssetKey action_c {};
  action_c.guid[0] = 0xCC;

  InputMappingContextAssetDesc desc {};
  desc.header.asset_type
    = static_cast<uint8_t>(AssetType::kInputMappingContext);
  desc.header.version
    = oxygen::data::pak::input::kInputMappingContextAssetVersion;
  std::snprintf(desc.header.name, sizeof(desc.header.name), "%s", "Gameplay");

  desc.mappings.offset = kDescSize;
  desc.mappings.count = 1;
  desc.mappings.entry_size = static_cast<uint32_t>(kMappingSize);
  desc.triggers.offset = kDescSize + kMappingSize;
  desc.triggers.count = 1;
  desc.triggers.entry_size = static_cast<uint32_t>(kTriggerSize);
  desc.trigger_aux.offset = kDescSize + kMappingSize + kTriggerSize;
  desc.trigger_aux.count = 1;
  desc.trigger_aux.entry_size = static_cast<uint32_t>(kAuxSize);
  desc.strings.offset = kDescSize + kMappingSize + kTriggerSize + kAuxSize;

  static constexpr char kStrings[] = "\0Keyboard.PageUp\0";
  desc.strings.count = static_cast<uint32_t>(sizeof(kStrings) - 1);
  desc.strings.entry_size = 1;

  InputActionMappingRecord mapping {};
  mapping.action_asset_key = action_a;
  mapping.slot_name_offset = 1;
  mapping.trigger_start_index = 0;
  mapping.trigger_count = 1;

  InputTriggerRecord trigger {};
  trigger.type = InputTriggerType::kPressed;
  trigger.behavior = InputTriggerBehavior::kImplicit;
  trigger.linked_action_asset_key = action_b;
  trigger.aux_start_index = 0;
  trigger.aux_count = 1;

  InputTriggerAuxRecord aux {};
  aux.action_asset_key = action_c;
  aux.completion_states = 3;
  aux.time_to_complete_ns = 1234;
  aux.flags = 5;

  WriteBlob(desc);
  WriteBlob(mapping);
  WriteBlob(trigger);
  WriteBlob(aux);
  ASSERT_TRUE(writer_.WriteBlob(
    std::as_bytes(std::span(kStrings, sizeof(kStrings) - 1))));
  ASSERT_TRUE(writer_.Flush());

  auto [context, collector] = MakeDecodeContext();
  const auto asset = LoadInputMappingContextAsset(context);
  ASSERT_THAT(asset, ::testing::NotNull());
  ASSERT_EQ(asset->GetMappings().size(), 1U);
  ASSERT_EQ(asset->GetTriggers().size(), 1U);
  ASSERT_EQ(asset->GetTriggerAuxRecords().size(), 1U);

  const auto slot_name
    = asset->TryGetString(asset->GetMappings()[0].slot_name_offset);
  ASSERT_TRUE(slot_name.has_value());
  EXPECT_EQ(*slot_name, "Keyboard.PageUp");

  const auto& deps = collector->AssetDependencies();
  EXPECT_THAT(deps, ::testing::Contains(action_a));
  EXPECT_THAT(deps, ::testing::Contains(action_b));
  EXPECT_THAT(deps, ::testing::Contains(action_c));
}

NOLINT_TEST_F(InputLoadersTest, LoadInputActionAssetInvalidValueTypeThrows)
{
  using oxygen::content::loaders::LoadInputActionAsset;
  using oxygen::data::AssetType;
  using oxygen::data::pak::input::InputActionAssetDesc;

  InputActionAssetDesc desc {};
  desc.header.asset_type = static_cast<uint8_t>(AssetType::kInputAction);
  desc.header.version = oxygen::data::pak::input::kInputActionAssetVersion;
  desc.value_type = 7;

  WriteBlob(desc);
  ASSERT_TRUE(writer_.Flush());

  auto context = MakeParseOnlyContext();
  EXPECT_THROW({ (void)LoadInputActionAsset(context); }, std::runtime_error);
}

NOLINT_TEST_F(
  InputLoadersTest, LoadInputMappingContextAssetInvalidTriggerTypeThrows)
{
  using oxygen::content::loaders::LoadInputMappingContextAsset;
  using oxygen::data::AssetType;
  using oxygen::data::pak::input::InputActionMappingRecord;
  using oxygen::data::pak::input::InputMappingContextAssetDesc;
  using oxygen::data::pak::input::InputTriggerBehavior;
  using oxygen::data::pak::input::InputTriggerRecord;
  using oxygen::data::pak::input::InputTriggerType;

  constexpr size_t kDescSize = sizeof(InputMappingContextAssetDesc);
  constexpr size_t kMappingSize = sizeof(InputActionMappingRecord);
  constexpr size_t kTriggerSize = sizeof(InputTriggerRecord);

  InputMappingContextAssetDesc desc {};
  desc.header.asset_type
    = static_cast<uint8_t>(AssetType::kInputMappingContext);
  desc.header.version
    = oxygen::data::pak::input::kInputMappingContextAssetVersion;
  desc.mappings.offset = kDescSize;
  desc.mappings.count = 1;
  desc.mappings.entry_size = static_cast<uint32_t>(kMappingSize);
  desc.triggers.offset = kDescSize + kMappingSize;
  desc.triggers.count = 1;
  desc.triggers.entry_size = static_cast<uint32_t>(kTriggerSize);
  desc.trigger_aux.offset = desc.triggers.offset + kTriggerSize;
  desc.trigger_aux.count = 0;
  desc.trigger_aux.entry_size
    = sizeof(oxygen::data::pak::input::InputTriggerAuxRecord);
  desc.strings.offset = desc.trigger_aux.offset;
  desc.strings.count = 2;
  desc.strings.entry_size = 1;

  InputActionMappingRecord mapping {};
  mapping.slot_name_offset = 1;
  mapping.trigger_start_index = 0;
  mapping.trigger_count = 1;

  InputTriggerRecord trigger {};
  trigger.type = InputTriggerType::kChord; // explicitly unsupported by loader
  trigger.behavior = InputTriggerBehavior::kImplicit;

  static constexpr char kStrings[] = "\0\0";
  WriteBlob(desc);
  WriteBlob(mapping);
  WriteBlob(trigger);
  ASSERT_TRUE(writer_.WriteBlob(
    std::as_bytes(std::span(kStrings, sizeof(kStrings) - 1))));
  ASSERT_TRUE(writer_.Flush());

  auto context = MakeParseOnlyContext();
  EXPECT_THROW(
    { (void)LoadInputMappingContextAsset(context); }, std::runtime_error);
}

NOLINT_TEST_F(
  InputLoadersTest, LoadInputMappingContextAssetInvalidSlotOffsetThrows)
{
  using oxygen::content::loaders::LoadInputMappingContextAsset;
  using oxygen::data::AssetType;
  using oxygen::data::pak::input::InputActionMappingRecord;
  using oxygen::data::pak::input::InputMappingContextAssetDesc;

  constexpr size_t kDescSize = sizeof(InputMappingContextAssetDesc);
  constexpr size_t kMappingSize = sizeof(InputActionMappingRecord);

  InputMappingContextAssetDesc desc {};
  desc.header.asset_type
    = static_cast<uint8_t>(AssetType::kInputMappingContext);
  desc.header.version
    = oxygen::data::pak::input::kInputMappingContextAssetVersion;
  desc.mappings.offset = kDescSize;
  desc.mappings.count = 1;
  desc.mappings.entry_size = static_cast<uint32_t>(kMappingSize);
  desc.triggers.offset = kDescSize + kMappingSize;
  desc.triggers.count = 0;
  desc.triggers.entry_size
    = sizeof(oxygen::data::pak::input::InputTriggerRecord);
  desc.trigger_aux.offset = desc.triggers.offset;
  desc.trigger_aux.count = 0;
  desc.trigger_aux.entry_size
    = sizeof(oxygen::data::pak::input::InputTriggerAuxRecord);
  desc.strings.offset = desc.trigger_aux.offset;
  desc.strings.count = 2;
  desc.strings.entry_size = 1;

  InputActionMappingRecord mapping {};
  mapping.slot_name_offset = 5; // out of bounds for 2-byte string table
  mapping.trigger_start_index = 0;
  mapping.trigger_count = 0;

  static constexpr char kStrings[] = "\0\0";
  WriteBlob(desc);
  WriteBlob(mapping);
  ASSERT_TRUE(writer_.WriteBlob(
    std::as_bytes(std::span(kStrings, sizeof(kStrings) - 1))));
  ASSERT_TRUE(writer_.Flush());

  auto context = MakeParseOnlyContext();
  EXPECT_THROW(
    { (void)LoadInputMappingContextAsset(context); }, std::runtime_error);
}

NOLINT_TEST_F(
  InputLoadersTest, LoadInputMappingContextAssetTriggerRangeOutOfBoundsThrows)
{
  using oxygen::content::loaders::LoadInputMappingContextAsset;
  using oxygen::data::AssetType;
  using oxygen::data::pak::input::InputActionMappingRecord;
  using oxygen::data::pak::input::InputMappingContextAssetDesc;

  constexpr size_t kDescSize = sizeof(InputMappingContextAssetDesc);
  constexpr size_t kMappingSize = sizeof(InputActionMappingRecord);

  InputMappingContextAssetDesc desc {};
  desc.header.asset_type
    = static_cast<uint8_t>(AssetType::kInputMappingContext);
  desc.header.version
    = oxygen::data::pak::input::kInputMappingContextAssetVersion;
  desc.mappings.offset = kDescSize;
  desc.mappings.count = 1;
  desc.mappings.entry_size = static_cast<uint32_t>(kMappingSize);
  desc.triggers.offset = kDescSize + kMappingSize;
  desc.triggers.count = 0;
  desc.triggers.entry_size
    = sizeof(oxygen::data::pak::input::InputTriggerRecord);
  desc.trigger_aux.offset = desc.triggers.offset;
  desc.trigger_aux.count = 0;
  desc.trigger_aux.entry_size
    = sizeof(oxygen::data::pak::input::InputTriggerAuxRecord);
  desc.strings.offset = desc.trigger_aux.offset;
  desc.strings.count = 2;
  desc.strings.entry_size = 1;

  InputActionMappingRecord mapping {};
  mapping.slot_name_offset = 1;
  mapping.trigger_start_index = 0;
  mapping.trigger_count = 1; // out of bounds because triggers.count == 0

  static constexpr char kStrings[] = "\0\0";
  WriteBlob(desc);
  WriteBlob(mapping);
  ASSERT_TRUE(writer_.WriteBlob(
    std::as_bytes(std::span(kStrings, sizeof(kStrings) - 1))));
  ASSERT_TRUE(writer_.Flush());

  auto context = MakeParseOnlyContext();
  EXPECT_THROW(
    { (void)LoadInputMappingContextAsset(context); }, std::runtime_error);
}

NOLINT_TEST_F(
  InputLoadersTest, LoadInputMappingContextAssetComboAuxOutOfBoundsThrows)
{
  using oxygen::content::loaders::LoadInputMappingContextAsset;
  using oxygen::data::AssetType;
  using oxygen::data::pak::input::InputActionMappingRecord;
  using oxygen::data::pak::input::InputMappingContextAssetDesc;
  using oxygen::data::pak::input::InputTriggerBehavior;
  using oxygen::data::pak::input::InputTriggerRecord;
  using oxygen::data::pak::input::InputTriggerType;

  constexpr size_t kDescSize = sizeof(InputMappingContextAssetDesc);
  constexpr size_t kMappingSize = sizeof(InputActionMappingRecord);
  constexpr size_t kTriggerSize = sizeof(InputTriggerRecord);

  InputMappingContextAssetDesc desc {};
  desc.header.asset_type
    = static_cast<uint8_t>(AssetType::kInputMappingContext);
  desc.header.version
    = oxygen::data::pak::input::kInputMappingContextAssetVersion;
  desc.mappings.offset = kDescSize;
  desc.mappings.count = 1;
  desc.mappings.entry_size = static_cast<uint32_t>(kMappingSize);
  desc.triggers.offset = kDescSize + kMappingSize;
  desc.triggers.count = 1;
  desc.triggers.entry_size = static_cast<uint32_t>(kTriggerSize);
  desc.trigger_aux.offset = desc.triggers.offset + kTriggerSize;
  desc.trigger_aux.count = 0;
  desc.trigger_aux.entry_size
    = sizeof(oxygen::data::pak::input::InputTriggerAuxRecord);
  desc.strings.offset = desc.trigger_aux.offset;
  desc.strings.count = 2;
  desc.strings.entry_size = 1;

  InputActionMappingRecord mapping {};
  mapping.slot_name_offset = 1;
  mapping.trigger_start_index = 0;
  mapping.trigger_count = 1;

  InputTriggerRecord trigger {};
  trigger.type = InputTriggerType::kCombo;
  trigger.behavior = InputTriggerBehavior::kImplicit;
  trigger.aux_start_index = 0;
  trigger.aux_count = 1; // out of bounds because trigger_aux.count == 0

  static constexpr char kStrings[] = "\0\0";
  WriteBlob(desc);
  WriteBlob(mapping);
  WriteBlob(trigger);
  ASSERT_TRUE(writer_.WriteBlob(
    std::as_bytes(std::span(kStrings, sizeof(kStrings) - 1))));
  ASSERT_TRUE(writer_.Flush());

  auto context = MakeParseOnlyContext();
  EXPECT_THROW(
    { (void)LoadInputMappingContextAsset(context); }, std::runtime_error);
}

NOLINT_TEST_F(
  InputLoadersTest, LoadInputMappingContextAssetDecodeWithoutCollectorThrows)
{
  using oxygen::content::loaders::LoadInputMappingContextAsset;
  using oxygen::data::AssetType;
  using oxygen::data::pak::input::InputMappingContextAssetDesc;

  InputMappingContextAssetDesc desc {};
  desc.header.asset_type
    = static_cast<uint8_t>(AssetType::kInputMappingContext);
  desc.header.version
    = oxygen::data::pak::input::kInputMappingContextAssetVersion;
  desc.strings.offset = sizeof(InputMappingContextAssetDesc);
  desc.strings.count = 1;
  desc.strings.entry_size = 1;

  static constexpr char kStrings[] = "\0";
  WriteBlob(desc);
  ASSERT_TRUE(writer_.WriteBlob(
    std::as_bytes(std::span(kStrings, sizeof(kStrings) - 1))));
  ASSERT_TRUE(writer_.Flush());

  auto context = MakeParseOnlyContext();
  context.parse_only = false;
  context.dependency_collector.reset();
  EXPECT_THROW(
    { (void)LoadInputMappingContextAsset(context); }, std::runtime_error);
}

} // namespace
