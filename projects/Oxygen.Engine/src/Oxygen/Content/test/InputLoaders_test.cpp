//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Content/ContentLoadError.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/InputActionLoader.h>
#include <Oxygen/Content/Loaders/InputMappingContextLoader.h>
#include <Oxygen/Cooker/testing/AssetDescriptors.h>
#include <Oxygen/Cooker/testing/DescriptorDefects.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/PakFormat_input.h>
#include <Oxygen/Data/SourceOrigin.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::ContentLoadError;
using oxygen::content::ContentLoadFailure;
using oxygen::cooker::testing::DescriptorDefects;
using oxygen::cooker::testing::InputActionDescriptor;
using oxygen::cooker::testing::InputContextBuilder;
using oxygen::data::CookedDefect;
namespace data = oxygen::data;
namespace input = oxygen::data::pak::input;
namespace loaders = oxygen::content::loaders;

//! The defect `load` refuses its descriptor with for a rule of its format;
//! empty when it throws nothing. Any other exception fails the test.
auto DefectOf(auto&& load) -> std::optional<CookedDefect>
{
  try {
    load();
  } catch (const oxygen::data::CookedFormatError& error) {
    return error.Defect();
  }
  return std::nullopt;
}

//! The failure `load` refuses its descriptor with for a rule of the engine;
//! empty when it throws nothing. Any other exception fails the test.
auto FailureOf(auto&& load) -> std::optional<ContentLoadFailure>
{
  try {
    load();
  } catch (const ContentLoadError& error) {
    return error.Failure();
  }
  return std::nullopt;
}

auto Key(const char* path) -> data::AssetKey
{
  return data::AssetKey::FromVirtualPath(path);
}

auto Jump() -> data::AssetKey { return Key("/Game/Input/Jump.oiact"); }

auto Crouch() -> data::AssetKey { return Key("/Game/Input/Crouch.oiact"); }

//! What the product loader `load` reads from `bytes`: a parse-only read or,
//! given a `collector`, a decode that records the dependencies.
template <typename Load>
auto Read(const std::span<const std::byte> bytes, Load load,
  std::shared_ptr<oxygen::content::internal::DependencyCollector> collector
  = {})
{
  auto stream = oxygen::serio::ReadOnlyMemoryStream { bytes };
  auto reader = oxygen::serio::Reader<oxygen::serio::ReadOnlyMemoryStream> {
    stream,
  };
  const auto parse_only = collector == nullptr;
  return load(oxygen::content::LoaderContext {
    .current_asset_key = {},
    .source_instance = data::SourceInstanceId(11U),
    .desc_reader = &reader,
    .work_offline = true,
    .dependency_collector = std::move(collector),
    .parse_only = parse_only,
  });
}

auto ContextDefect(const std::vector<std::byte>& bytes)
  -> std::optional<CookedDefect>
{
  return DefectOf(
    [&] { (void)Read(bytes, loaders::LoadInputMappingContextAsset); });
}

auto ContextFailure(const std::vector<std::byte>& bytes)
  -> std::optional<ContentLoadFailure>
{
  return FailureOf(
    [&] { (void)Read(bytes, loaders::LoadInputMappingContextAsset); });
}

auto ActionDefect(const std::vector<std::byte>& bytes)
  -> std::optional<CookedDefect>
{
  return DefectOf([&] { (void)Read(bytes, loaders::LoadInputActionAsset); });
}

auto ActionFailure(const std::vector<std::byte>& bytes)
  -> std::optional<ContentLoadFailure>
{
  return FailureOf([&] { (void)Read(bytes, loaders::LoadInputActionAsset); });
}

//! A context that maps Jump to Space through one default trigger.
auto JumpContext() -> std::vector<std::byte>
{
  auto context = InputContextBuilder { "Gameplay" };
  context.Mapping(Jump(), "Space", { input::InputTriggerRecord {} });
  return context.Bytes();
}

//! A context that maps Jump to Space through a combo of one step, Crouch.
auto ComboContext() -> std::vector<std::byte>
{
  auto step = input::InputTriggerAuxRecord {};
  step.action_asset_key = Crouch();
  auto context = InputContextBuilder { "Gameplay" };
  context.Combo(Jump(), "Space", { step });
  return context.Bytes();
}

//! A context that maps Jump to Space through a trigger of `type`, linked to
//! Crouch; then makes the trigger's link nil.
auto NilLinkedContext(const input::InputTriggerType type)
  -> std::vector<std::byte>
{
  auto trigger = input::InputTriggerRecord {};
  trigger.type = type;
  trigger.linked_action_asset_key = Crouch();
  auto context = InputContextBuilder { "Gameplay" };
  context.Mapping(Jump(), "Space", { trigger });
  auto bytes = context.Bytes();
  trigger.linked_action_asset_key = {};
  DescriptorDefects::SetInputTrigger(bytes, 0U, trigger);
  return bytes;
}

//=== Valid descriptors ===---------------------------------------------------//

//! An action descriptor loads with its name, value type and flags.
NOLINT_TEST(InputLoadersTest, LoadInputActionAssetValidDescriptorParses)
{
  const auto action = InputActionDescriptor("Look",
    {
      .value_type = input::InputActionValueType::kAxis2D,
      .flags = input::InputActionAssetFlags::kConsumesInput
        | input::InputActionAssetFlags::kCumulativeAccumulation,
    });

  const auto asset = Read(action.bytes, loaders::LoadInputActionAsset);

  ASSERT_THAT(asset, ::testing::NotNull());
  EXPECT_EQ(asset->GetAssetName(), "Look");
  EXPECT_EQ(asset->GetValueType(), input::InputActionValueType::kAxis2D);
  EXPECT_TRUE(asset->ConsumesInput());
  EXPECT_TRUE(asset->AccumulatesCumulatively());
}

//! A decode of a context records the actions its mappings, trigger links and
//! combo steps name as dependencies, and reads its slot names back.
NOLINT_TEST(InputLoadersTest,
  LoadInputMappingContextAssetDecodeCollectsActionDependencies)
{
  const auto linked = Key("/Game/Input/Look.oiact");
  auto trigger = input::InputTriggerRecord {};
  trigger.linked_action_asset_key = linked;
  auto step = input::InputTriggerAuxRecord {};
  step.action_asset_key = Crouch();
  auto context = InputContextBuilder { "Gameplay" };
  context.Combo(Jump(), "PageUp", { step }, trigger);
  auto collector
    = std::make_shared<oxygen::content::internal::DependencyCollector>();

  const auto asset
    = Read(context.Bytes(), loaders::LoadInputMappingContextAsset, collector);

  ASSERT_THAT(asset, ::testing::NotNull());
  ASSERT_EQ(asset->GetMappings().size(), 1U);
  ASSERT_EQ(asset->GetTriggers().size(), 1U);
  ASSERT_EQ(asset->GetTriggerAuxRecords().size(), 1U);
  EXPECT_EQ(asset->TryGetString(asset->GetMappings().front().slot_name_offset),
    std::optional<std::string_view> { "PageUp" });
  const auto& deps = collector->AssetDependencies();
  EXPECT_THAT(deps, ::testing::Contains(Jump()));
  EXPECT_THAT(deps, ::testing::Contains(linked));
  EXPECT_THAT(deps, ::testing::Contains(Crouch()));
}

//! Every trigger type the input schema declares loads, the chord and the
//! action chain included.
NOLINT_TEST(InputLoadersTest, LoadInputMappingContextAssetAcceptsChord)
{
  for (const auto type : {
         input::InputTriggerType::kChord,
         input::InputTriggerType::kActionChain,
       }) {
    auto trigger = input::InputTriggerRecord {};
    trigger.type = type;
    trigger.linked_action_asset_key = Crouch();
    auto context = InputContextBuilder { "Gameplay" };
    context.Mapping(Jump(), "Space", { trigger });

    const auto asset
      = Read(context.Bytes(), loaders::LoadInputMappingContextAsset);

    ASSERT_THAT(asset, ::testing::NotNull());
    EXPECT_EQ(asset->GetTriggers().front().type, type);
  }
}

//=== Format defects ===------------------------------------------------------//

//! An action of another descriptor version is a format defect.
NOLINT_TEST(InputLoadersTest, LoadInputActionAssetStaleVersionIsADefect)
{
  auto bytes = InputActionDescriptor("Jump").bytes;
  DescriptorDefects::SetVersion(
    bytes, static_cast<uint8_t>(input::kInputActionAssetVersion + 1U));

  EXPECT_EQ(ActionDefect(bytes), CookedDefect::kUnsupportedVersion);
}

//! An action descriptor whose header names another asset type is a format
//! defect.
NOLINT_TEST(InputLoadersTest, LoadInputActionAssetWrongAssetTypeIsADefect)
{
  auto bytes = InputActionDescriptor("Jump").bytes;
  DescriptorDefects::SetAssetType(bytes, data::AssetType::kInputMappingContext);

  EXPECT_EQ(ActionDefect(bytes), CookedDefect::kWrongMagic);
}

//! A value type no format version defines is a format defect.
NOLINT_TEST(InputLoadersTest, LoadInputActionAssetInvalidValueTypeIsADefect)
{
  auto bytes = InputActionDescriptor("Jump").bytes;
  auto desc = DescriptorDefects::InputActionAt(bytes);
  desc.value_type = static_cast<input::InputActionValueType>(7U);
  DescriptorDefects::SetInputAction(bytes, desc);

  EXPECT_EQ(ActionDefect(bytes), CookedDefect::kInvalidValue);
}

//! A context of another descriptor version is a format defect.
NOLINT_TEST(InputLoadersTest, LoadInputMappingContextAssetStaleVersionIsADefect)
{
  auto bytes = JumpContext();
  DescriptorDefects::SetVersion(
    bytes, static_cast<uint8_t>(input::kInputMappingContextAssetVersion + 1U));

  EXPECT_EQ(ContextDefect(bytes), CookedDefect::kUnsupportedVersion);
}

//! A context descriptor whose header names another asset type is a format
//! defect.
NOLINT_TEST(
  InputLoadersTest, LoadInputMappingContextAssetWrongAssetTypeIsADefect)
{
  auto bytes = JumpContext();
  DescriptorDefects::SetAssetType(bytes, data::AssetType::kInputAction);

  EXPECT_EQ(ContextDefect(bytes), CookedDefect::kWrongMagic);
}

//! Context flags are reserved: a descriptor that sets one is a format defect.
NOLINT_TEST(
  InputLoadersTest, LoadInputMappingContextAssetReservedFlagsAreADefect)
{
  auto bytes = JumpContext();
  auto desc = DescriptorDefects::InputContextAt(bytes);
  desc.flags = static_cast<input::InputMappingContextFlags>(1U);
  DescriptorDefects::SetInputContext(bytes, desc);

  EXPECT_EQ(ContextDefect(bytes), CookedDefect::kReservedNotZero);
}

//! A mapping table whose row size is not the record's is a format defect.
NOLINT_TEST(
  InputLoadersTest, LoadInputMappingContextAssetMappingRowSizeIsADefect)
{
  auto bytes = JumpContext();
  auto desc = DescriptorDefects::InputContextAt(bytes);
  desc.mappings.entry_size += 1U;
  DescriptorDefects::SetInputContext(bytes, desc);

  EXPECT_EQ(ContextDefect(bytes), CookedDefect::kWrongRowSize);
}

//! A trigger table whose row size is not the record's is a format defect.
NOLINT_TEST(
  InputLoadersTest, LoadInputMappingContextAssetTriggerRowSizeIsADefect)
{
  auto bytes = JumpContext();
  auto desc = DescriptorDefects::InputContextAt(bytes);
  desc.triggers.entry_size += 1U;
  DescriptorDefects::SetInputContext(bytes, desc);

  EXPECT_EQ(ContextDefect(bytes), CookedDefect::kWrongRowSize);
}

//! A combo step table whose row size is not the record's is a format defect.
NOLINT_TEST(InputLoadersTest, LoadInputMappingContextAssetComboRowSizeIsADefect)
{
  auto bytes = ComboContext();
  auto desc = DescriptorDefects::InputContextAt(bytes);
  desc.trigger_aux.entry_size += 1U;
  DescriptorDefects::SetInputContext(bytes, desc);

  EXPECT_EQ(ContextDefect(bytes), CookedDefect::kWrongRowSize);
}

//! A context without a string table is a format defect.
NOLINT_TEST(
  InputLoadersTest, LoadInputMappingContextAssetNoStringTableIsADefect)
{
  auto bytes = JumpContext();
  auto desc = DescriptorDefects::InputContextAt(bytes);
  desc.header.strings.size = 0U;
  DescriptorDefects::SetInputContext(bytes, desc);

  EXPECT_EQ(ContextDefect(bytes), CookedDefect::kOutOfBounds);
}

//! A slot name that starts past the string table is a format defect.
NOLINT_TEST(InputLoadersTest, LoadInputMappingContextAssetSlotOffsetIsADefect)
{
  auto bytes = JumpContext();
  auto mapping = DescriptorDefects::InputMappingAt(bytes, 0U);
  mapping.slot_name_offset = 1000U;
  DescriptorDefects::SetInputMapping(bytes, 0U, mapping);

  EXPECT_EQ(ContextDefect(bytes), CookedDefect::kOutOfBounds);
}

//! A trigger type no format version defines is a format defect.
NOLINT_TEST(
  InputLoadersTest, LoadInputMappingContextAssetInvalidTriggerTypeIsADefect)
{
  auto bytes = JumpContext();
  auto trigger = DescriptorDefects::InputTriggerAt(bytes, 0U);
  trigger.type = static_cast<input::InputTriggerType>(0xFFU);
  DescriptorDefects::SetInputTrigger(bytes, 0U, trigger);

  EXPECT_EQ(ContextDefect(bytes), CookedDefect::kInvalidValue);
}

//! A trigger behavior no format version defines is a format defect.
NOLINT_TEST(
  InputLoadersTest, LoadInputMappingContextAssetInvalidTriggerBehaviorIsADefect)
{
  auto bytes = JumpContext();
  auto trigger = DescriptorDefects::InputTriggerAt(bytes, 0U);
  trigger.behavior = static_cast<input::InputTriggerBehavior>(0xFFU);
  DescriptorDefects::SetInputTrigger(bytes, 0U, trigger);

  EXPECT_EQ(ContextDefect(bytes), CookedDefect::kInvalidValue);
}

//! A mapping whose trigger range runs past the trigger table is a format
//! defect.
NOLINT_TEST(InputLoadersTest,
  LoadInputMappingContextAssetTriggerRangeOutOfBoundsIsADefect)
{
  auto bytes = JumpContext();
  auto mapping = DescriptorDefects::InputMappingAt(bytes, 0U);
  mapping.trigger_count += 1U;
  DescriptorDefects::SetInputMapping(bytes, 0U, mapping);

  EXPECT_EQ(ContextDefect(bytes), CookedDefect::kInvalidReference);
}

//! A combo trigger whose step range runs past the step table is a format
//! defect.
NOLINT_TEST(InputLoadersTest,
  LoadInputMappingContextAssetComboStepRangeOutOfBoundsIsADefect)
{
  auto bytes = ComboContext();
  auto trigger = DescriptorDefects::InputTriggerAt(bytes, 0U);
  trigger.aux_count += 1U;
  DescriptorDefects::SetInputTrigger(bytes, 0U, trigger);

  EXPECT_EQ(ContextDefect(bytes), CookedDefect::kInvalidReference);
}

//=== Engine rules ===--------------------------------------------------------//

//! An action without a name breaks an engine rule: contexts and scripts find
//! an action by its name.
NOLINT_TEST(InputLoadersTest, LoadInputActionAssetEmptyNameIsInvalidContent)
{
  auto bytes = InputActionDescriptor("Jump").bytes;
  auto desc = DescriptorDefects::InputActionAt(bytes);
  // Offset 0 of a descriptor's string table is the empty string.
  desc.header.name_offset = 0U;
  DescriptorDefects::SetInputAction(bytes, desc);

  EXPECT_EQ(ActionFailure(bytes), ContentLoadFailure::kContentInvalid);
}

//! A mapping on a slot the platform does not register breaks an engine rule.
NOLINT_TEST(
  InputLoadersTest, LoadInputMappingContextAssetUnknownSlotIsInvalidContent)
{
  auto bytes = JumpContext();
  DescriptorDefects::ReplaceString(bytes, "Space", "Spacx");

  EXPECT_EQ(ContextFailure(bytes), ContentLoadFailure::kContentInvalid);
}

//! A mapping that names no action breaks an engine rule.
NOLINT_TEST(
  InputLoadersTest, LoadInputMappingContextAssetNilActionIsInvalidContent)
{
  auto bytes = JumpContext();
  auto mapping = DescriptorDefects::InputMappingAt(bytes, 0U);
  mapping.action_asset_key = {};
  DescriptorDefects::SetInputMapping(bytes, 0U, mapping);

  EXPECT_EQ(ContextFailure(bytes), ContentLoadFailure::kContentInvalid);
}

//! A chord trigger that links no action breaks an engine rule.
NOLINT_TEST(
  InputLoadersTest, LoadInputMappingContextAssetNilChordLinkIsInvalidContent)
{
  EXPECT_EQ(ContextFailure(NilLinkedContext(input::InputTriggerType::kChord)),
    ContentLoadFailure::kContentInvalid);
}

//! An action chain trigger that links no action breaks an engine rule.
NOLINT_TEST(
  InputLoadersTest, LoadInputMappingContextAssetNilChainLinkIsInvalidContent)
{
  EXPECT_EQ(
    ContextFailure(NilLinkedContext(input::InputTriggerType::kActionChain)),
    ContentLoadFailure::kContentInvalid);
}

//! A combo step that names no action breaks an engine rule.
NOLINT_TEST(
  InputLoadersTest, LoadInputMappingContextAssetNilComboStepIsInvalidContent)
{
  auto bytes = ComboContext();
  auto step = DescriptorDefects::InputTriggerAuxAt(bytes, 0U);
  step.action_asset_key = {};
  DescriptorDefects::SetInputTriggerAux(bytes, 0U, step);

  EXPECT_EQ(ContextFailure(bytes), ContentLoadFailure::kContentInvalid);
}

} // namespace
