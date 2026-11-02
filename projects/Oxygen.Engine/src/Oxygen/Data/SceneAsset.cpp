//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/LodPolicyValidation.h>
#include <Oxygen/Data/PakFormatSerioLoaders.h> // IWYU pragma: keep
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/SourceOrigin.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::data {

namespace {

  //! Refuses the descriptor for breaking the rule `rule` describes.
  [[noreturn]] auto Refuse(
    const CookedDefect defect, const std::string_view rule) -> void
  {
    throw CookedOpenError(defect, "SceneAsset " + std::string(rule));
  }

  //! Decodes the record `RecordT` that `bytes` hold exactly; its decoder
  //! checks the values the format defines.
  template <typename RecordT>
  auto ReadPackedRecord(const std::span<const std::byte> bytes,
    const std::string_view what) -> RecordT
  {
    oxygen::serio::ReadOnlyMemoryStream stream(bytes);
    oxygen::serio::Reader reader(stream);
    auto packed = reader.ScopedAlignment(1);

    RecordT record {};
    if (!reader.ReadInto(record)) {
      Refuse(CookedDefect::kInvalidValue,
        std::string(what) + " holds values its format does not define");
    }
    return record;
  }

  //! Whether `size` bytes at `offset` lie within `total` bytes.
  auto RangeFits(const std::size_t offset, const std::size_t size,
    const std::size_t total) noexcept -> bool
  {
    return offset <= total && size <= total - offset;
  }

  //! Calls `visit.template operator()<Record>()` for each type of `Records`.
  template <typename Records, typename Visit>
  auto ForEachRecordType(const Visit& visit) -> void
  {
    [&visit]<typename... Record>(std::type_identity<std::tuple<Record...>>) {
      (visit.template operator()<Record>(), ...);
    }(std::type_identity<Records> {});
  }

  //! Whether `slots` already has an owner of the atmosphere light slot of
  //! `record`, claiming it otherwise.
  template <typename LightRecord>
  auto ClaimsATakenAtmosphereSlot(const LightRecord& record,
    std::array<bool, schema::kAtmosphereLightSlotMembers.size()>& slots) -> bool
  {
    if constexpr (requires { record.atmosphere_light_slot; }) {
      const auto slot = record.atmosphere_light_slot;
      if (slot == std::to_underlying(schema::AtmosphereLightSlot::kNone)) {
        return false;
      }
      if (slot >= slots.size()) {
        Refuse(CookedDefect::kInvalidValue,
          "light names an atmosphere slot the format does not define");
      }
      return std::exchange(slots.at(slot), true);
    } else {
      return false;
    }
  }

} // namespace

auto SceneAsset::ReadScriptSlots(const uint32_t start,
  const uint32_t count) const -> std::vector<pak::scripting::ScriptSlotRecord>
{
  if (start > desc_.script_slots.count
    || count > desc_.script_slots.count - start) {
    throw std::out_of_range("Scene script slot range is out of bounds");
  }
  std::vector<pak::scripting::ScriptSlotRecord> slots;
  slots.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    const auto offset = desc_.script_slots.offset
      + (uint64_t { start + i } * sizeof(pak::scripting::ScriptSlotRecord));
    slots.push_back(ReadPackedRecord<pak::scripting::ScriptSlotRecord>(
      data_.subspan(
        static_cast<size_t>(offset), sizeof(pak::scripting::ScriptSlotRecord)),
      "script slot"));
  }
  return slots;
}

auto SceneAsset::ReadScriptParameters(
  const pak::scripting::ScriptSlotRecord& slot) const
  -> std::vector<pak::scripting::ScriptParamRecord>
{
  const auto size = uint64_t { slot.params_count }
    * sizeof(pak::scripting::ScriptParamRecord);
  if (slot.params_array_offset > data_.size()
    || size > data_.size() - slot.params_array_offset) {
    throw std::out_of_range("Scene script parameter range is out of bounds");
  }
  std::vector<pak::scripting::ScriptParamRecord> parameters;
  parameters.reserve(slot.params_count);
  for (uint32_t i = 0; i < slot.params_count; ++i) {
    const auto offset = slot.params_array_offset
      + (uint64_t { i } * sizeof(pak::scripting::ScriptParamRecord));
    parameters.push_back(ReadPackedRecord<pak::scripting::ScriptParamRecord>(
      data_.subspan(
        static_cast<size_t>(offset), sizeof(pak::scripting::ScriptParamRecord)),
      "script parameter"));
  }
  return parameters;
}

SceneAsset::SceneAsset(
  AssetKey key, std::span<const std::byte> data, SourceOrigin source_origin)
  : Asset(key, source_origin)
  , data_(data)
{
  ParseAndValidate();
}

SceneAsset::SceneAsset(
  AssetKey key, std::vector<std::byte> data, SourceOrigin source_origin)
  : Asset(key, source_origin)
  , owned_data_(std::make_shared<std::vector<std::byte>>(std::move(data)))
  , data_(owned_data_->data(), owned_data_->size())
{
  ParseAndValidate();
}

auto SceneAsset::GetNodeName(const pak::world::NodeRecord& node) const noexcept
  -> std::string_view
{
  return strings_.TryGet(node.scene_name_offset).value_or(std::string_view {});
}

// An index out of range is a caller bug; the checked access aborts on it.
// NOLINTNEXTLINE(bugprone-exception-escape)
auto SceneAsset::GetNode(pak::world::SceneNodeIndexT index) const noexcept
  -> const pak::world::NodeRecord&
{
  return oxygen::base::CheckedAt(GetNodes(), index);
}

// Calling this on a scene without nodes is a caller bug; the checked access
// aborts on it.
// NOLINTNEXTLINE(bugprone-exception-escape)
auto SceneAsset::GetRootNode() const noexcept -> const pak::world::NodeRecord&
{
  return oxygen::base::CheckedAt(GetNodes(), 0);
}

auto SceneAsset::ParseAndValidate() -> void
{
  ReadHeader();
  ReadStrings();
  ReadNodes();
  ValidateHierarchy();
  ReadComponentTables();
  ValidateNodeTables();
  ValidateComponentOwners();
  ReadScriptTables();
  ReadEnvironment();

  std::unordered_set<pak::world::SceneNodeIndexT> instance_roots;
  for (const auto& instance :
    GetComponents<pak::world::SceneInstanceRecord>()) {
    instance_roots.insert(instance.node_index);
  }
  ValidateNodeIdentity();
  if (const auto camera = GetStartingCamera()) {
    ValidateNodeRef(*camera, instance_roots, "starting camera");
  }
  ValidateScriptNodes(instance_roots);
  ValidateOverrides(instance_roots);
}

auto SceneAsset::ReadHeader() -> void
{
  using pak::world::SceneAssetDesc;
  if (data_.size() < sizeof(SceneAssetDesc)) {
    Refuse(CookedDefect::kTooSmall, "descriptor is smaller than its header");
  }
  desc_ = ReadPackedRecord<SceneAssetDesc>(
    data_.first(sizeof(SceneAssetDesc)), "header");
  if (static_cast<AssetType>(desc_.header.asset_type) != AssetType::kScene) {
    Refuse(CookedDefect::kWrongMagic, "descriptor is not a scene's");
  }
  if (desc_.header.version != pak::world::kSceneAssetVersion) {
    Refuse(CookedDefect::kUnsupportedVersion,
      "descriptor version is not current; re-cook the scene content");
  }
  const auto has_unknown_flags
    = (desc_.flags & ~pak::world::SceneAssetFlags::kModelScene)
    != pak::world::SceneAssetFlags::kNone;
  if (has_unknown_flags) {
    Refuse(CookedDefect::kInvalidValue, "has unknown scene flags");
  }
  payload_end_ = sizeof(SceneAssetDesc);
}

auto SceneAsset::ReadStrings() -> void
{
  try {
    strings_ = DescriptorStrings::FromDescriptor(data_, desc_.header.strings);
  } catch (const std::invalid_argument& error) {
    Refuse(CookedDefect::kInvalidString,
      std::string("string table is invalid: ") + error.what());
  }
  const auto name = strings_.TryGet(desc_.header.name_offset);
  if (!name) {
    Refuse(CookedDefect::kInvalidString, "name offset is not a string");
  }
  SetAssetName(std::string { *name });
  payload_end_ = (std::max)(payload_end_,
    std::size_t { desc_.header.strings.offset } + desc_.header.strings.size);
}

auto SceneAsset::ReadNodes() -> void
{
  using pak::world::NodeRecord;
  const auto& table = desc_.nodes;
  if (table.count == 0U) {
    return;
  }
  const auto bytes = std::size_t { table.count } * sizeof(NodeRecord);
  if (!RangeFits(table.offset, bytes, data_.size())) {
    Refuse(
      CookedDefect::kOutOfBounds, "node table lies outside the descriptor");
  }
  if (table.entry_size != sizeof(NodeRecord)) {
    Refuse(CookedDefect::kWrongRowSize, "node table rows are not node records");
  }
  nodes_.reserve(table.count);
  for (uint32_t index = 0; index < table.count; ++index) {
    const auto record = ReadPackedRecord<NodeRecord>(
      data_.subspan(table.offset + (std::size_t { index } * sizeof(NodeRecord)),
        sizeof(NodeRecord)),
      "node record");
    if (!pak::world::HasCanonicalNodeFlags(record)) {
      Refuse(CookedDefect::kInvalidValue, "node flag source state is invalid");
    }
    if (!strings_.TryGet(record.scene_name_offset)) {
      Refuse(CookedDefect::kInvalidString, "node name offset is not a string");
    }
    nodes_.push_back(record);
  }
  payload_end_ = (std::max)(payload_end_, table.offset + bytes);
}

auto SceneAsset::ValidateHierarchy() const -> void
{
  // Node 0 is the scene's root, and every parent chain ends at a root: a node
  // that is its own parent.
  if (nodes_.empty()) {
    return;
  }
  if (nodes_.front().parent_index != 0U) {
    Refuse(CookedDefect::kInvalidReference, "node 0 is not a root");
  }
  enum class Visit : uint8_t { kUnseen, kOnTrail, kDone };
  auto visits = std::vector<Visit>(nodes_.size(), Visit::kUnseen);
  auto trail = std::vector<pak::world::SceneNodeIndexT> {};
  for (pak::world::SceneNodeIndexT start = 0; start < nodes_.size(); ++start) {
    auto at = start;
    while (visits.at(at) == Visit::kUnseen) {
      const auto parent = nodes_.at(at).parent_index;
      if (parent >= nodes_.size()) {
        Refuse(CookedDefect::kInvalidReference,
          "node parent is not a node of the scene");
      }
      visits.at(at) = Visit::kOnTrail;
      trail.push_back(at);
      if (parent == at) {
        break;
      }
      at = parent;
    }
    const auto closes_a_cycle
      = visits.at(at) == Visit::kOnTrail && nodes_.at(at).parent_index != at;
    if (closes_a_cycle) {
      Refuse(CookedDefect::kInvalidReference, "node parents form a cycle");
    }
    for (const auto node : trail) {
      visits.at(node) = Visit::kDone;
    }
    trail.clear();
  }
}

auto SceneAsset::ReadComponentTables() -> void
{
  using pak::world::SceneComponentTableDesc;
  const auto count = desc_.component_table_count;
  if (count == 0U) {
    return;
  }
  const auto directory_bytes
    = std::size_t { count } * sizeof(SceneComponentTableDesc);
  const auto directory_offset
    = std::size_t { desc_.component_table_directory_offset };
  if (!RangeFits(directory_offset, directory_bytes, data_.size())) {
    Refuse(CookedDefect::kOutOfBounds,
      "component directory lies outside the descriptor");
  }
  payload_end_ = (std::max)(payload_end_, directory_offset + directory_bytes);

  const auto read_table
    = [this]<typename T>(const pak::world::SceneComponentTable& table) {
        if (table.count == 0U) {
          return;
        }
        if (table.entry_size != sizeof(T)) {
          Refuse(CookedDefect::kWrongRowSize,
            "component table rows are not its type's records");
        }
        const auto bytes = std::size_t { table.count } * sizeof(T);
        if (!RangeFits(table.offset, bytes, data_.size())) {
          Refuse(CookedDefect::kOutOfBounds,
            "component table lies outside the descriptor");
        }
        auto& records = std::get<std::vector<T>>(component_tables_);
        records.reserve(table.count);
        for (uint32_t index = 0; index < table.count; ++index) {
          records.push_back(ReadPackedRecord<T>(
            data_.subspan(
              table.offset + (std::size_t { index } * sizeof(T)), sizeof(T)),
            "component record"));
        }
        payload_end_ = (std::max)(payload_end_, table.offset + bytes);
      };

  // The directory names each type the format defines at most once; only then
  // are its tables read.
  std::vector<SceneComponentTableDesc> entries;
  entries.reserve(count);
  std::unordered_set<uint32_t> types;
  for (uint32_t index = 0; index < count; ++index) {
    const auto& entry
      = entries.emplace_back(ReadPackedRecord<SceneComponentTableDesc>(
        data_.subspan(directory_offset
            + (std::size_t { index } * sizeof(SceneComponentTableDesc)),
          sizeof(SceneComponentTableDesc)),
        "component directory entry"));
    auto known = false;
    ForEachRecordType<SceneComponentRecords>([&]<typename T> {
      known = known
        || entry.component_type
          == nostd::to_underlying(ComponentTraits<T>::kType);
    });
    if (!known) {
      Refuse(CookedDefect::kInvalidValue,
        "component directory names a type the format does not define");
    }
    if (!types.insert(entry.component_type).second) {
      Refuse(CookedDefect::kDuplicate,
        "component directory names a table type twice");
    }
  }
  for (const auto& entry : entries) {
    ForEachRecordType<SceneComponentRecords>([&]<typename T> {
      if (entry.component_type
        == nostd::to_underlying(ComponentTraits<T>::kType)) {
        read_table.template operator()<T>(entry.table);
      }
    });
  }
}

auto SceneAsset::ValidateNodeTables() const -> void
{
  // A node table holds at most one record per node, in node order, which the
  // lookup by node relies on.
  ForEachRecordType<SceneComponentRecords>([this]<typename T> {
    if constexpr (NodeComponentRecord<T>) {
      std::optional<pak::world::SceneNodeIndexT> previous;
      for (const auto& record : GetComponents<T>()) {
        if (record.node_index >= nodes_.size()) {
          Refuse(CookedDefect::kInvalidReference,
            "component names a node the scene does not hold");
        }
        const auto repeats_its_node
          = previous.has_value() && record.node_index == *previous;
        const auto precedes_its_node
          = previous.has_value() && record.node_index < *previous;
        if (repeats_its_node) {
          Refuse(CookedDefect::kDuplicate, "node has two components of a type");
        }
        if (precedes_its_node) {
          Refuse(CookedDefect::kNotCanonical,
            "component table is not in node order");
        }
        previous = record.node_index;
      }
    }
  });
}

auto SceneAsset::ValidateComponentOwners() const -> void
{
  // A node owns at most one camera and one light, and an atmosphere slot has
  // at most one light.
  std::unordered_set<pak::world::SceneNodeIndexT> camera_nodes;
  const auto own_camera = [&camera_nodes](const auto& cameras) {
    for (const auto& camera : cameras) {
      if (!camera_nodes.insert(camera.node_index).second) {
        Refuse(CookedDefect::kDuplicate, "node owns two cameras");
      }
    }
  };
  own_camera(GetComponents<pak::world::PerspectiveCameraRecord>());
  own_camera(GetComponents<pak::world::OrthographicCameraRecord>());

  std::unordered_set<pak::world::SceneNodeIndexT> light_nodes;
  // Slots are indexed by wire value; an assigned slot has one owner.
  std::array<bool, schema::kAtmosphereLightSlotMembers.size()>
    atmosphere_slots {};
  const auto own_light = [&](const auto& lights) {
    for (const auto& light : lights) {
      if (!light_nodes.insert(light.node_index).second) {
        Refuse(CookedDefect::kDuplicate, "node owns two lights");
      }
      if (ClaimsATakenAtmosphereSlot(light, atmosphere_slots)) {
        Refuse(CookedDefect::kDuplicate, "atmosphere slot has two lights");
      }
    }
  };
  own_light(GetComponents<pak::world::DirectionalLightRecord>());
  own_light(GetComponents<pak::world::PointLightRecord>());
  own_light(GetComponents<pak::world::SpotLightRecord>());

  for (const auto& instance :
    GetComponents<pak::world::SceneInstanceRecord>()) {
    if (instance.scene_key.IsNil()) {
      Refuse(CookedDefect::kMissingPart, "scene instance names no scene");
    }
  }
  std::unordered_set<AssetKey> contexts;
  for (const auto& context :
    GetComponents<pak::world::StartingInputContextRecord>()) {
    if (context.context_key.IsNil()) {
      Refuse(CookedDefect::kMissingPart, "starting input context has no key");
    }
    if (!contexts.insert(context.context_key).second) {
      Refuse(CookedDefect::kDuplicate, "starting input context appears twice");
    }
  }

  // An instanced starting camera is checked when the scene is composed.
  const auto& camera = desc_.starting_camera;
  const auto names_an_own_node_without_camera
    = camera.node_index != pak::world::kNoStartingCamera
    && camera.node_path == pak::world::kNoNodePath
    && !camera_nodes.contains(camera.node_index);
  if (names_an_own_node_without_camera) {
    Refuse(
      CookedDefect::kInvalidReference, "starting camera is not a camera node");
  }
}

auto SceneAsset::ReadScriptTables() -> void
{
  using pak::scripting::ScriptParamRecord;
  using pak::scripting::ScriptSlotRecord;
  const auto& table = desc_.script_slots;
  if (table.count == 0U) {
    if (table.offset != 0U || table.entry_size != 0U) {
      Refuse(CookedDefect::kNotCanonical,
        "empty script slot table does not use the empty range");
    }
  } else {
    const auto slots_size = uint64_t { table.count } * sizeof(ScriptSlotRecord);
    if (table.entry_size != sizeof(ScriptSlotRecord)) {
      Refuse(
        CookedDefect::kWrongRowSize, "script slot rows are not slot records");
    }
    if (table.offset < payload_end_) {
      Refuse(CookedDefect::kOverlap, "script slots overlap the tables");
    }
    if (!RangeFits(table.offset, slots_size, data_.size())) {
      Refuse(
        CookedDefect::kOutOfBounds, "script slots lie outside the descriptor");
    }
    payload_end_ = table.offset + slots_size;
    // Each slot's parameters follow the slots, in slot order.
    for (const auto& slot : ReadScriptSlots(0U, table.count)) {
      if (slot.script_asset_key.IsNil()) {
        Refuse(CookedDefect::kMissingPart, "script slot names no script");
      }
      if (slot.slot_id.IsNil()) {
        Refuse(CookedDefect::kMissingPart, "script slot has no slot ID");
      }
      const auto params_size
        = uint64_t { slot.params_count } * sizeof(ScriptParamRecord);
      if (params_size == 0U) {
        if (slot.params_array_offset != 0U) {
          Refuse(CookedDefect::kNotCanonical,
            "empty script parameters do not use the empty range");
        }
        continue;
      }
      if (slot.params_array_offset != payload_end_) {
        Refuse(CookedDefect::kOverlap,
          "script parameters do not follow the slots without gaps or overlap");
      }
      if (!RangeFits(slot.params_array_offset, params_size, data_.size())) {
        Refuse(CookedDefect::kOutOfBounds,
          "script parameters lie outside the descriptor");
      }
      payload_end_ += params_size;
    }
  }

  // The scripting components own the slots, each a contiguous range of its
  // own, together all of them.
  std::vector<std::pair<uint32_t, uint32_t>> ranges;
  for (const auto& component :
    GetComponents<pak::scripting::ScriptingComponentRecord>()) {
    if (component.slot_start_index > table.count
      || component.slot_count > table.count - component.slot_start_index) {
      Refuse(CookedDefect::kOutOfBounds,
        "scripting component names slots the scene does not hold");
    }
    if (component.slot_count != 0U) {
      ranges.emplace_back(component.slot_start_index,
        component.slot_start_index + component.slot_count);
    }
    // A change of a slot's parameter names the slot by its ID.
    std::unordered_set<ScriptSlotId> slot_ids;
    for (const auto& slot :
      ReadScriptSlots(component.slot_start_index, component.slot_count)) {
      if (!slot_ids.insert(slot.slot_id).second) {
        Refuse(CookedDefect::kDuplicate, "node has two script slots of one ID");
      }
    }
  }
  std::ranges::sort(ranges);
  uint32_t next_slot = 0;
  for (const auto [begin, end] : ranges) {
    if (begin != next_slot) {
      Refuse(
        CookedDefect::kOverlap, "script slot owners leave gaps or overlap");
    }
    next_slot = end;
  }
  if (next_slot != table.count) {
    Refuse(CookedDefect::kOverlap, "script slot table holds unowned slots");
  }
}

auto SceneAsset::ReadEnvironment() -> void
{
  using pak::world::SceneEnvironmentBlockHeader;
  using pak::world::SceneEnvironmentSystemRecordHeader;
  // The environment block starts where the payload ends and ends the
  // descriptor.
  if (!RangeFits(
        payload_end_, sizeof(SceneEnvironmentBlockHeader), data_.size())) {
    Refuse(CookedDefect::kMissingPart, "has no environment block");
  }
  environment_block_header_ = ReadPackedRecord<SceneEnvironmentBlockHeader>(
    data_.subspan(payload_end_, sizeof(SceneEnvironmentBlockHeader)),
    "environment block header");
  const auto& block = environment_block_header_;
  const auto block_ends_the_descriptor
    = block.byte_size >= sizeof(SceneEnvironmentBlockHeader)
    && block.byte_size == data_.size() - payload_end_;
  if (!block_ends_the_descriptor) {
    Refuse(CookedDefect::kSizeMismatch,
      "environment block does not end the descriptor");
  }
  const auto record_room
    = (block.byte_size - sizeof(SceneEnvironmentBlockHeader))
    / sizeof(SceneEnvironmentSystemRecordHeader);
  if (block.systems_count > record_room) {
    Refuse(CookedDefect::kOutOfBounds,
      "environment block counts more records than it holds");
  }

  const auto read_record = [this]<typename T>(const std::size_t offset,
                             const std::span<const std::byte> bytes) {
    auto& stored = std::get<std::optional<T>>(environment_records_);
    if (stored.has_value()) {
      Refuse(CookedDefect::kDuplicate, "environment holds a record twice");
    }
    const auto record
      = ReadPackedRecord<T>(bytes.first(sizeof(T)), "environment record");
    if constexpr (requires { record.source; }) {
      if (!pak::world::IsKnownSource(record.source)) {
        Refuse(CookedDefect::kInvalidValue,
          "sky record names a source the format does not define");
      }
    }
    if constexpr (std::is_same_v<T,
                    pak::world::PostProcessVolumeEnvironmentRecord>) {
      // The record's decoder checks that its size holds its curve keys.
      using Key = pak::world::ExposureCompensationKeyRecord;
      post_process_curve_.reserve(record.curve_key_count);
      for (uint32_t index = 0; index < record.curve_key_count; ++index) {
        const auto key = ReadPackedRecord<Key>(
          bytes.subspan(
            sizeof(T) + (std::size_t { index } * sizeof(Key)), sizeof(Key)),
          "exposure compensation key");
        if (!post_process_curve_.empty()
          && key.metered_ev <= post_process_curve_.back().metered_ev) {
          Refuse(CookedDefect::kNotCanonical,
            "exposure curve EV keys do not strictly increase");
        }
        post_process_curve_.push_back(key);
      }
    }
    stored = record;
    environment_system_records_.push_back(EnvironmentSystemRecordView {
      .header = record.header,
      .record_offset = offset,
      .bytes = bytes,
    });
  };

  environment_system_records_.reserve(block.systems_count);
  auto cursor = payload_end_ + sizeof(SceneEnvironmentBlockHeader);
  for (uint32_t index = 0; index < block.systems_count; ++index) {
    if (!RangeFits(
          cursor, sizeof(SceneEnvironmentSystemRecordHeader), data_.size())) {
      Refuse(CookedDefect::kOutOfBounds,
        "environment record header lies outside the block");
    }
    const auto header = ReadPackedRecord<SceneEnvironmentSystemRecordHeader>(
      data_.subspan(cursor, sizeof(SceneEnvironmentSystemRecordHeader)),
      "environment record header");
    if (!pak::world::IsKnownEnvironmentSystemType(header.system_type)) {
      Refuse(CookedDefect::kInvalidValue,
        "environment record type is not one the format defines");
    }
    // A valid size holds at least the record's header.
    if (!pak::world::IsValidEnvironmentRecordSize(header)) {
      Refuse(CookedDefect::kSizeMismatch,
        "environment record size is not its type's");
    }
    if (!RangeFits(cursor, header.record_size, data_.size())) {
      Refuse(CookedDefect::kOutOfBounds,
        "environment record lies outside the block");
    }
    const auto bytes = data_.subspan(cursor, header.record_size);
    ForEachRecordType<SceneEnvironmentRecords>([&]<typename T> {
      if (header.system_type
        == nostd::to_underlying(EnvironmentTraits<T>::kType)) {
        read_record.template operator()<T>(cursor, bytes);
      }
    });
    cursor += header.record_size;
  }
  if (cursor != data_.size()) {
    Refuse(CookedDefect::kSizeMismatch,
      "environment block holds bytes after its records");
  }
}

auto SceneAsset::GetNodePath(const pak::world::NodeRef& ref) const
  -> std::vector<NodeId>
{
  if (ref.node_path == pak::world::kNoNodePath) {
    return {};
  }
  const auto paths = GetComponents<pak::world::NodePathRecord>();
  const auto elements = GetComponents<pak::world::NodePathElement>();
  const auto& path = oxygen::base::CheckedAt(paths, ref.node_path);
  std::vector<NodeId> ids;
  ids.reserve(path.count);
  for (const auto& element : elements.subspan(path.first, path.count)) {
    ids.push_back(element.node_id);
  }
  return ids;
}

auto SceneAsset::ValidateNodeIdentity() const -> void
{
  // Composition finds nodes by ID: an ID names one node of the scene.
  std::unordered_set<NodeId> ids;
  for (const auto& node : nodes_) {
    if (node.node_id.IsNil()) {
      Refuse(CookedDefect::kMissingPart, "node has no node ID");
    }
    if (!ids.insert(node.node_id).second) {
      Refuse(CookedDefect::kDuplicate, "two nodes share a node ID");
    }
  }
  const auto elements = GetComponents<pak::world::NodePathElement>();
  if (std::ranges::any_of(
        elements, [](const pak::world::NodePathElement& element) -> bool {
          return element.node_id.IsNil();
        })) {
    Refuse(CookedDefect::kMissingPart, "node path element has no node ID");
  }
  // A path names one node, so two paths name two nodes.
  std::vector<std::vector<NodeId>> paths;
  for (const auto& path : GetComponents<pak::world::NodePathRecord>()) {
    if (path.count == 0U || path.first > elements.size()
      || path.count > elements.size() - path.first) {
      Refuse(
        CookedDefect::kOutOfBounds, "node path runs outside its element table");
    }
    auto& path_ids = paths.emplace_back();
    for (const auto& element : elements.subspan(path.first, path.count)) {
      path_ids.push_back(element.node_id);
    }
  }
  std::ranges::sort(paths);
  if (std::ranges::adjacent_find(paths) != paths.end()) {
    Refuse(CookedDefect::kDuplicate, "two node paths name one node");
  }
}

auto SceneAsset::ValidateNodeRef(const pak::world::NodeRef& ref,
  const std::unordered_set<pak::world::SceneNodeIndexT>& instance_roots,
  const std::string_view what) const -> void
{
  if (ref.node_index >= nodes_.size()) {
    Refuse(CookedDefect::kInvalidReference,
      std::string(what) + " names a node out of range");
  }
  if (ref.node_path == pak::world::kNoNodePath) {
    return;
  }
  if (ref.node_path >= GetComponents<pak::world::NodePathRecord>().size()) {
    Refuse(CookedDefect::kInvalidReference,
      std::string(what) + " names a node path out of range");
  }
  if (!instance_roots.contains(ref.node_index)) {
    Refuse(CookedDefect::kInvalidReference,
      std::string(what)
        + " has a node path below a node that is not an instance root");
  }
}

auto SceneAsset::ValidateScriptNodes(
  const std::unordered_set<pak::world::SceneNodeIndexT>& instance_roots) const
  -> void
{
  using pak::scripting::ScriptParamType;
  const auto validate_param
    = [&](const pak::scripting::ScriptParamRecord& param) -> void {
    if (!TryGetScriptParamKey(param, strings_)) {
      Refuse(
        CookedDefect::kInvalidString, "script parameter key is not a string");
    }
    if (!ToScriptParam(param, strings_)) {
      Refuse(CookedDefect::kInvalidValue, "script parameter value is invalid");
    }
    if (param.type == ScriptParamType::kNode
      && param.value.as_node.node_index != pak::scripting::kUnsetNodeParam) {
      ValidateNodeRef(param.value.as_node, instance_roots, "node parameter");
    }
  };
  if (desc_.script_slots.count != 0U) {
    for (const auto& slot : ReadScriptSlots(0U, desc_.script_slots.count)) {
      for (const auto& param : ReadScriptParameters(slot)) {
        validate_param(param);
      }
    }
  }

  const pak::scripting::ScriptParamOverrideRecord* previous = nullptr;
  for (const auto& change :
    GetComponents<pak::scripting::ScriptParamOverrideRecord>()) {
    if (change.target.node_path == pak::world::kNoNodePath) {
      Refuse(CookedDefect::kInconsistent,
        "script parameter change does not name an instanced node");
    }
    ValidateNodeRef(change.target, instance_roots, "script parameter change");
    if (change.slot_id.IsNil()) {
      Refuse(CookedDefect::kMissingPart, "script parameter change has no slot");
    }
    validate_param(change.param);
    if (previous != nullptr) {
      const auto previous_key
        = strings_.TryGet(previous->param.key_offset).value_or("");
      const auto key = strings_.TryGet(change.param.key_offset).value_or("");
      const auto previous_address
        = std::tie(previous->target, previous->slot_id, previous_key);
      const auto address = std::tie(change.target, change.slot_id, key);
      if (previous_address == address) {
        Refuse(CookedDefect::kDuplicate,
          "two script parameter changes name one node, slot and key");
      }
      if (previous_address > address) {
        Refuse(CookedDefect::kNotCanonical,
          "script parameter changes are not sorted by node, slot and key");
      }
    }
    previous = &change;
  }
}

auto SceneAsset::ValidateOverrides(
  const std::unordered_set<pak::world::SceneNodeIndexT>& instance_roots) const
  -> void
{
  std::unordered_set<pak::world::SceneNodeIndexT> renderable_nodes;
  for (const auto& renderable : GetComponents<pak::world::RenderableRecord>()) {
    renderable_nodes.insert(renderable.node_index);
  }
  const auto lod_policies = GetComponents<pak::geometry::LodPolicyRecord>();
  std::vector<bool> lod_policy_used(lod_policies.size(), false);
  const pak::world::OverrideRecord* previous = nullptr;
  for (const auto& record : GetComponents<pak::world::OverrideRecord>()) {
    const auto* property = schema::FindOverrideProperty(record.override_id);
    if (property == nullptr) {
      Refuse(CookedDefect::kInvalidValue, "override names an unknown property");
    }
    ValidateNodeRef(record.target, instance_roots, "override");
    if (record.target.node_path == pak::world::kNoNodePath) {
      // A node of this scene stores only its overrides of its asset's values;
      // every other value lives in the node's own records.
      if (!property->asset_sourced) {
        Refuse(CookedDefect::kInconsistent,
          "override names a property the node's records hold");
      }
      // Every asset-sourced property belongs to the geometry component
      // today; a property of another component needs its own owner check.
      if (property->component_kind != std::string_view { "geometry" }) {
        Refuse(CookedDefect::kInconsistent,
          "override names a component no asset sources");
      }
      if (!renderable_nodes.contains(record.target.node_index)) {
        Refuse(
          CookedDefect::kInvalidReference, "override node has no renderable");
      }
    }
    switch (property->part_kind) {
    case schema::PartKind::kNone:
      if (!record.part_id.IsNil() || !base::IsAllZero(record.layout_revision)) {
        Refuse(CookedDefect::kInconsistent, "component override names a part");
      }
      break;
    case schema::PartKind::kSurface:
      if (record.part_id.IsNil() || base::IsAllZero(record.layout_revision)) {
        Refuse(CookedDefect::kMissingPart,
          "surface override needs its surface and layout revision");
      }
      break;
    }
    if (previous != nullptr) {
      const auto previous_address
        = std::tie(previous->target, previous->override_id, previous->part_id);
      const auto address
        = std::tie(record.target, record.override_id, record.part_id);
      if (previous_address == address) {
        Refuse(CookedDefect::kDuplicate,
          "two overrides name one target, property and part");
      }
      if (previous_address > address) {
        Refuse(CookedDefect::kNotCanonical,
          "overrides are not sorted by target, property and part");
      }
    }
    previous = &record;
    ValidateOverrideValue(*property, record.value, lod_policy_used);
  }
  if (std::ranges::find(lod_policy_used, false) != lod_policy_used.end()) {
    Refuse(CookedDefect::kInconsistent,
      "LOD policy records and the overrides naming them do not pair");
  }
}

auto SceneAsset::ValidateOverrideValue(const schema::OverrideProperty& property,
  const pak::world::OverrideValue& value,
  std::vector<bool>& lod_policy_used) const -> void
{
  const auto bytes = std::as_bytes(std::span { &value, 1 });
  const auto require_zero_after = [bytes](const std::size_t used) -> void {
    const auto unused_bytes_are_zero = std::ranges::all_of(bytes.subspan(used),
      [](const std::byte byte) -> bool { return byte == std::byte { 0 }; });
    if (!unused_bytes_are_zero) {
      Refuse(CookedDefect::kReservedNotZero,
        "override value bytes past its type are not zero");
    }
  };
  // The range of a change of a wired property is checked when it is applied,
  // with the record it patches.
  const auto finite = [](const std::span<const float> values) -> bool {
    return std::ranges::all_of(
      values, [](const float number) -> bool { return std::isfinite(number); });
  };
  switch (property.member) {
  case schema::ValueMember::kBool:
    if (value.as_bool > 1U) {
      Refuse(CookedDefect::kInvalidValue, "override bool value is invalid");
    }
    require_zero_after(sizeof(value.as_bool));
    return;
  case schema::ValueMember::kInt32:
    require_zero_after(sizeof(value.as_int32));
    return;
  case schema::ValueMember::kFloat:
    if (!finite(std::span(&value.as_float, 1))) {
      Refuse(CookedDefect::kInvalidValue, "override float value is invalid");
    }
    require_zero_after(sizeof(value.as_float));
    return;
  case schema::ValueMember::kVec3:
    if (!finite(value.as_vec3)) {
      Refuse(CookedDefect::kInvalidValue, "override vector value is invalid");
    }
    require_zero_after(sizeof(value.as_vec3));
    return;
  case schema::ValueMember::kQuat:
    if (!finite(value.as_quat)) {
      Refuse(CookedDefect::kInvalidValue, "override rotation is invalid");
    }
    return;
  case schema::ValueMember::kAsset:
    if (value.as_asset.IsNil()) {
      Refuse(CookedDefect::kMissingPart, "override asset value is nil");
    }
    return;
  case schema::ValueMember::kRecordIndex: {
    require_zero_after(sizeof(value.as_record_index));
    const auto index = value.as_record_index.get();
    const auto lod_policies = GetComponents<pak::geometry::LodPolicyRecord>();
    if (index >= lod_policies.size()) {
      Refuse(CookedDefect::kInvalidReference,
        "override names a LOD policy the scene does not hold");
    }
    if (lod_policy_used.at(index)) {
      Refuse(CookedDefect::kInconsistent,
        "LOD policy records and the overrides naming them do not pair");
    }
    if (!IsValidLodPolicy(lod_policies[index])) {
      Refuse(CookedDefect::kInvalidValue, "override LOD policy is invalid");
    }
    lod_policy_used.at(index) = true;
    return;
  }
  case schema::ValueMember::kStringOffset:
    // No changeable property holds a string.
    Refuse(CookedDefect::kInvalidValue, "override value type is not supported");
  }
  Refuse(CookedDefect::kInvalidValue, "override value type is unknown");
}

} // namespace oxygen::data
