//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Content/Loaders/InputActionLoader.h>
#include <Oxygen/Content/Loaders/InputMappingContextLoader.h>
#include <Oxygen/Content/Loaders/PhysicsAssetLoader.h>
#include <Oxygen/Cooker/Import/Internal/AssetReferenceBuilder.h>
#include <Oxygen/Cooker/testing/AssetDescriptors.h>
#include <Oxygen/Cooker/testing/Descriptors.h>
#include <Oxygen/Cooker/testing/ProductLoad.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/DescriptorHash.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat_input.h>
#include <Oxygen/Data/PakFormat_physics.h>

namespace oxygen::cooker::testing {

namespace {

  namespace input = data::pak::input;
  namespace physics = data::pak::physics;

  //! The descriptor of one packed record named `name`, as a cook writes it.
  template <typename Record>
  auto Named(Record record, const data::AssetType type, const uint8_t version,
    const std::string_view name) -> std::vector<std::byte>
  {
    record.header.asset_type = static_cast<uint8_t>(type);
    record.header.version = version;
    auto strings = data::DescriptorStringsBuilder {};
    record.header.name_offset = strings.Add(name);
    auto bytes = data::MakeDescriptorBytes(record, strings);
    data::StampDescriptorHash(bytes);
    return bytes;
  }

} // namespace

auto PhysicsMaterialDescriptor(const std::string_view name,
  const physics::PhysicsMaterialAssetDesc record) -> DescriptorFixture
{
  auto fixture = DescriptorFixture {
    .bytes = Named(record, data::AssetType::kPhysicsMaterial,
      physics::kPhysicsMaterialAssetVersion, name),
    .references = {},
  };
  detail::CheckWithLoader("physics material '" + std::string(name) + "'",
    fixture.bytes, fixture.references,
    content::loaders::LoadPhysicsMaterialDescriptor);
  return fixture;
}

auto CollisionShapeDescriptor(const std::string_view name,
  const physics::CollisionShapeAssetDesc record) -> DescriptorFixture
{
  if (record.shape_type == physics::ShapeType::kCompound) {
    throw std::invalid_argument("the collision shape '" + std::string(name)
      + "' is a compound, which the kit does not build");
  }
  auto references = content::import::AssetReferenceBuilder {};
  references.AddAsset(
    record.material_asset_key, data::AssetType::kPhysicsMaterial);
  references.AddPhysicsResource(record.cooked_shape_ref.payload_asset_key);
  auto fixture = DescriptorFixture {
    .bytes = Named(record, data::AssetType::kCollisionShape,
      physics::kCollisionShapeAssetVersion, name),
    .references = std::move(references).Build(),
  };
  detail::CheckWithLoader("collision shape '" + std::string(name) + "'",
    fixture.bytes, fixture.references,
    content::loaders::LoadCollisionShapeDescriptor);
  return fixture;
}

auto InputActionDescriptor(const std::string_view name,
  const input::InputActionAssetDesc record) -> DescriptorFixture
{
  auto fixture = DescriptorFixture {
    .bytes = Named(record, data::AssetType::kInputAction,
      input::kInputActionAssetVersion, name),
    .references = {},
  };
  detail::CheckWithLoader("input action '" + std::string(name) + "'",
    fixture.bytes, fixture.references, content::loaders::LoadInputActionAsset);
  return fixture;
}

InputContextBuilder::InputContextBuilder(
  const std::string_view name, const input::InputMappingContextAssetDesc record)
  : name_(name)
  , record_(record)
{
}

auto InputContextBuilder::Mapping(const data::AssetKey& action,
  const std::string_view slot, std::vector<input::InputTriggerRecord> triggers,
  input::InputActionMappingRecord record) -> InputContextBuilder&
{
  record.action_asset_key = action;
  record.trigger_start_index = static_cast<uint32_t>(triggers_.size());
  record.trigger_count = static_cast<uint32_t>(triggers.size());
  triggers_.insert(triggers_.end(), triggers.begin(), triggers.end());
  mappings_.push_back({ .record = record, .slot = std::string(slot) });
  return *this;
}

auto InputContextBuilder::Combo(const data::AssetKey& action,
  const std::string_view slot, std::vector<input::InputTriggerAuxRecord> steps,
  input::InputTriggerRecord trigger) -> InputContextBuilder&
{
  trigger.type = input::InputTriggerType::kCombo;
  trigger.aux_start_index = static_cast<uint32_t>(steps_.size());
  trigger.aux_count = static_cast<uint32_t>(steps.size());
  steps_.insert(steps_.end(), steps.begin(), steps.end());
  return Mapping(action, slot, { trigger });
}

auto InputContextBuilder::Bytes() const -> std::vector<std::byte>
{
  // The cook's layout: the mapping, trigger and trigger aux tables after the
  // record, then the string table with the slot names.
  auto desc = record_;
  desc.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kInputMappingContext);
  desc.header.version = input::kInputMappingContextAssetVersion;
  auto strings = data::DescriptorStringsBuilder {};
  desc.header.name_offset = strings.Add(name_);
  auto mappings = std::vector<input::InputActionMappingRecord> {};
  for (const auto& mapped : mappings_) {
    auto record = mapped.record;
    record.slot_name_offset = strings.Add(mapped.slot);
    mappings.push_back(record);
  }
  desc.mappings = {
    .offset = sizeof(input::InputMappingContextAssetDesc),
    .count = static_cast<uint32_t>(mappings.size()),
    .entry_size = sizeof(input::InputActionMappingRecord),
  };
  desc.triggers = {
    .offset = desc.mappings.offset
      + static_cast<uint32_t>(mappings.size() * desc.mappings.entry_size),
    .count = static_cast<uint32_t>(triggers_.size()),
    .entry_size = sizeof(input::InputTriggerRecord),
  };
  desc.trigger_aux = {
    .offset = desc.triggers.offset
      + static_cast<uint32_t>(triggers_.size() * desc.triggers.entry_size),
    .count = static_cast<uint32_t>(steps_.size()),
    .entry_size = sizeof(input::InputTriggerAuxRecord),
  };
  auto bytes = std::vector<std::byte> {};
  AppendRecord(bytes, desc);
  for (const auto& record : mappings) {
    AppendRecord(bytes, record);
  }
  for (const auto& record : triggers_) {
    AppendRecord(bytes, record);
  }
  for (const auto& record : steps_) {
    AppendRecord(bytes, record);
  }
  data::AppendDescriptorStrings(bytes, strings);
  data::StampDescriptorHash(bytes);
  detail::CheckWithLoader("input mapping context '" + name_ + "'", bytes,
    References(), content::loaders::LoadInputMappingContextAsset);
  return bytes;
}

auto InputContextBuilder::References() const -> data::AssetReferences
{
  auto references = content::import::AssetReferenceBuilder {};
  for (const auto& mapped : mappings_) {
    references.AddAsset(
      mapped.record.action_asset_key, data::AssetType::kInputAction);
  }
  for (const auto& trigger : triggers_) {
    references.AddAsset(
      trigger.linked_action_asset_key, data::AssetType::kInputAction);
  }
  for (const auto& step : steps_) {
    references.AddAsset(step.action_asset_key, data::AssetType::kInputAction);
  }
  return std::move(references).Build();
}

} // namespace oxygen::cooker::testing
