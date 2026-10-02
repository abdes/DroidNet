//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/MaterialSlotId.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SourceOrigin.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::data {

namespace {

  template <typename RecordT>
  auto ReadPackedRecord(const std::span<const std::byte> bytes,
    const std::string_view what) -> RecordT
  {
    oxygen::serio::ReadOnlyMemoryStream stream(bytes);
    oxygen::serio::Reader reader(stream);
    auto packed = reader.ScopedAlignment(1);

    RecordT record {};
    const auto res = reader.ReadInto(record);
    if (!res) {
      throw std::runtime_error(std::string(what) + " decode failed");
    }
    return record;
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
      "Scene script slot"));
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
      "Scene script parameter"));
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

auto SceneAsset::GetNodes() const noexcept
  -> std::span<const pak::world::NodeRecord>
{
  if (node_count_ == 0) {
    return {};
  }

  if (!nodes_cache_valid_) {
    const size_t nodes_bytes = node_count_ * sizeof(pak::world::NodeRecord);
    const auto bytes = data_.subspan(desc_.nodes.offset, nodes_bytes);

    std::vector<std::byte> buffer;
    buffer.assign(bytes.begin(), bytes.end());

    oxygen::serio::MemoryStream stream { std::span<std::byte>(buffer) };
    oxygen::serio::Reader<oxygen::serio::MemoryStream> reader(stream);
    auto pack = reader.ScopedAlignment(1);

    nodes_cache_.clear();
    nodes_cache_.resize(node_count_);
    for (size_t i = 0; i < node_count_; ++i) {
      const auto res = reader.ReadInto(nodes_cache_.at(i));
      if (!res) {
        DCHECK_F(false,
          "SceneAsset failed to deserialize node table (validated by "
          "loader)");
        nodes_cache_.clear();
        return {};
      }
    }

    nodes_cache_valid_ = true;
  }

  return { nodes_cache_.data(), nodes_cache_.size() };
}

auto SceneAsset::GetNode(pak::world::SceneNodeIndexT index) const noexcept
  -> const pak::world::NodeRecord&
{
  const auto nodes = GetNodes();
  DCHECK_LT_F(index, nodes.size());
  return oxygen::base::CheckedAt(nodes, index);
}

auto SceneAsset::GetNodeName(const pak::world::NodeRecord& node) const noexcept
  -> std::string_view
{
  if (node.scene_name_offset >= string_table_size_) {
    return {};
  }

  const char* begin = string_table_ptr_ + node.scene_name_offset;
  const char* end = string_table_ptr_ + string_table_size_;
  const auto it = std::find(begin, end, '\0');
  return { begin, static_cast<size_t>(it - begin) };
}

auto SceneAsset::GetRootNode() const noexcept -> const pak::world::NodeRecord&
{
  const auto nodes = GetNodes();
  DCHECK_GT_F(nodes.size(), 0);
  return oxygen::base::CheckedAt(nodes, 0);
}

auto SceneAsset::ParseAndValidate() -> void
{
  if (data_.size() < sizeof(pak::world::SceneAssetDesc)) {
    throw std::runtime_error("SceneAsset data too small for header");
  }

  desc_ = ReadPackedRecord<pak::world::SceneAssetDesc>(
    data_.first(sizeof(pak::world::SceneAssetDesc)), "SceneAsset header");

  if (desc_.header.version != pak::world::kSceneAssetVersion) {
    throw std::runtime_error(
      "SceneAsset unsupported descriptor version; re-cook the scene content");
  }

  auto range_ok
    = [](const size_t offset, const size_t size, const size_t total) -> bool {
    return offset <= total && size <= (total - offset);
  };

  has_environment_block_ = false;
  environment_system_records_.clear();
  post_process_record_.reset();
  post_process_curve_.clear();

  size_t payload_end = sizeof(pak::world::SceneAssetDesc);

  // Validate Node Table
  if (desc_.nodes.count > 0) {
    const size_t nodes_bytes
      = static_cast<size_t>(desc_.nodes.count) * sizeof(pak::world::NodeRecord);
    if (!range_ok(desc_.nodes.offset, nodes_bytes, data_.size())) {
      throw std::runtime_error("SceneAsset node table out of bounds");
    }
    if (desc_.nodes.entry_size != sizeof(pak::world::NodeRecord)) {
      throw std::runtime_error("SceneAsset node record size mismatch");
    }

    for (uint32_t index = 0; index < desc_.nodes.count; ++index) {
      const auto record = ReadPackedRecord<pak::world::NodeRecord>(
        data_.subspan(desc_.nodes.offset
            + (static_cast<size_t>(index) * sizeof(pak::world::NodeRecord)),
          sizeof(pak::world::NodeRecord)),
        "SceneAsset node record");
      if (!pak::world::HasCanonicalNodeFlags(record)) {
        throw std::runtime_error(
          "SceneAsset node flag source state is invalid");
      }
      if (record.parent_index >= desc_.nodes.count) {
        throw std::runtime_error("SceneAsset parent_index out of range");
      }
      if (record.scene_name_offset != 0
        && record.scene_name_offset >= desc_.scene_strings.size) {
        throw std::runtime_error("SceneAsset node name offset out of range");
      }
    }

    payload_end = (std::max)(payload_end, desc_.nodes.offset + nodes_bytes);
  }

  // Validate String Table
  if (desc_.scene_strings.size > 0) {
    if (!range_ok(
          desc_.scene_strings.offset, desc_.scene_strings.size, data_.size())) {
      throw std::runtime_error("SceneAsset string table out of bounds");
    }

    // Minimal runtime-safety invariant: offset 0 must refer to empty string.
    const auto bytes
      = data_.subspan(desc_.scene_strings.offset, desc_.scene_strings.size);
    if (!bytes.empty()
      && oxygen::base::CheckedAt(bytes, 0) != std::byte { 0 }) {
      throw std::runtime_error(
        "SceneAsset string table must start with a NUL byte");
    }

    payload_end = (std::max)(payload_end,
      static_cast<size_t>(
        desc_.scene_strings.offset + desc_.scene_strings.size));
  }

  // Validate Component Directory
  if (desc_.component_table_count > 0) {
    const size_t dir_bytes = static_cast<size_t>(desc_.component_table_count)
      * sizeof(pak::world::SceneComponentTableDesc);
    if (!range_ok(
          desc_.component_table_directory_offset, dir_bytes, data_.size())) {
      throw std::runtime_error("SceneAsset component directory out of bounds");
    }

    payload_end = (std::max)(payload_end,
      static_cast<size_t>(desc_.component_table_directory_offset + dir_bytes));

    const auto dir_span
      = data_.subspan(desc_.component_table_directory_offset, dir_bytes);

    std::unordered_set<std::uint32_t> light_nodes;
    std::unordered_set<std::uint32_t> camera_nodes;
    std::array<bool, 3> atmosphere_slots {};
    component_tables_.clear();
    component_tables_.reserve(desc_.component_table_count);
    for (uint32_t i = 0; i < desc_.component_table_count; ++i) {
      const auto entry_bytes = dir_span.subspan(
        static_cast<size_t>(i) * sizeof(pak::world::SceneComponentTableDesc),
        sizeof(pak::world::SceneComponentTableDesc));
      const auto entry = ReadPackedRecord<pak::world::SceneComponentTableDesc>(
        entry_bytes, "SceneAsset component table descriptor");

      if (entry.table.count == 0) {
        continue;
      }

      const size_t table_bytes
        = static_cast<size_t>(entry.table.count) * entry.table.entry_size;
      if (!range_ok(entry.table.offset, table_bytes, data_.size())) {
        throw std::runtime_error("SceneAsset component table out of bounds");
      }

      payload_end = (std::max)(payload_end,
        static_cast<size_t>(entry.table.offset + table_bytes));

      const auto type = static_cast<ComponentType>(entry.component_type);
      if (type == ComponentType::kRenderable
        && entry.table.entry_size != sizeof(pak::world::RenderableRecord)) {
        throw std::runtime_error("SceneAsset renderable record size mismatch");
      }
      if (type == ComponentType::kMaterialOverride
        && entry.table.entry_size
          != sizeof(pak::world::MaterialOverrideRecord)) {
        throw std::runtime_error(
          "SceneAsset material override record size mismatch");
      }
      if (type == ComponentType::kLocalFogVolume
        && entry.table.entry_size != sizeof(pak::world::LocalFogVolumeRecord)) {
        throw std::runtime_error(
          "SceneAsset local fog volume record size mismatch");
      }
      if (type == ComponentType::kPerspectiveCamera
        && entry.table.entry_size
          != sizeof(pak::world::PerspectiveCameraRecord)) {
        throw std::runtime_error(
          "SceneAsset perspective camera record size mismatch");
      }
      if (type == ComponentType::kOrthographicCamera
        && entry.table.entry_size
          != sizeof(pak::world::OrthographicCameraRecord)) {
        throw std::runtime_error(
          "SceneAsset orthographic camera record size mismatch");
      }

      if (type == ComponentType::kDirectionalLight
        && entry.table.entry_size
          != sizeof(pak::world::DirectionalLightRecord)) {
        throw std::runtime_error(
          "SceneAsset directional light record size mismatch");
      }
      if (type == ComponentType::kPointLight
        && entry.table.entry_size != sizeof(pak::world::PointLightRecord)) {
        throw std::runtime_error("SceneAsset point light record size mismatch");
      }
      if (type == ComponentType::kSpotLight
        && entry.table.entry_size != sizeof(pak::world::SpotLightRecord)) {
        throw std::runtime_error("SceneAsset spot light record size mismatch");
      }

      if (type == ComponentType::kPerspectiveCamera
        || type == ComponentType::kOrthographicCamera) {
        for (uint32_t index = 0; index < entry.table.count; ++index) {
          const auto camera_bytes = data_.subspan(entry.table.offset
              + (static_cast<size_t>(index) * entry.table.entry_size),
            entry.table.entry_size);
          if (type == ComponentType::kPerspectiveCamera) {
            const auto record
              = ReadPackedRecord<pak::world::PerspectiveCameraRecord>(
                camera_bytes, "SceneAsset perspective camera");
            if (record.node_index >= desc_.nodes.count
              || !camera_nodes.insert(record.node_index).second) {
              throw std::runtime_error(
                "SceneAsset camera node ownership is invalid");
            }
          } else {
            const auto record
              = ReadPackedRecord<pak::world::OrthographicCameraRecord>(
                camera_bytes, "SceneAsset orthographic camera");
            if (record.node_index >= desc_.nodes.count
              || !camera_nodes.insert(record.node_index).second) {
              throw std::runtime_error(
                "SceneAsset camera node ownership is invalid");
            }
          }
        }
      }

      const auto validate_lights = [&]<typename T> -> auto {
        for (std::uint32_t index = 0U; index < entry.table.count; ++index) {
          const auto record = ReadPackedRecord<T>(
            data_.subspan(entry.table.offset
                + (static_cast<std::size_t>(index) * entry.table.entry_size),
              entry.table.entry_size),
            "SceneAsset light");
          if (record.node_index >= desc_.nodes.count
            || !light_nodes.insert(record.node_index).second) {
            throw std::runtime_error(
              "SceneAsset light node ownership is invalid");
          }
          if constexpr (requires { record.atmosphere_light_slot; }) {
            const auto slot = record.atmosphere_light_slot;
            if (slot != 0U && std::exchange(atmosphere_slots.at(slot), true)) {
              throw std::runtime_error(
                "SceneAsset atmosphere slot has multiple owners");
            }
          }
        }
      };
      if (type == ComponentType::kDirectionalLight) {
        validate_lights
          .template operator()<pak::world::DirectionalLightRecord>();
      } else if (type == ComponentType::kPointLight) {
        validate_lights.template operator()<pak::world::PointLightRecord>();
      } else if (type == ComponentType::kSpotLight) {
        validate_lights.template operator()<pak::world::SpotLightRecord>();
      }

      component_tables_.push_back({
        .type = type,
        .offset = entry.table.offset,
        .count = entry.table.count,
        .entry_size = entry.table.entry_size,
      });
    }
  }

  // Cache node and string table views.
  node_count_ = desc_.nodes.count;
  nodes_cache_valid_ = false;
  nodes_cache_.clear();

  string_table_size_ = desc_.scene_strings.size;
  string_table_ptr_ = string_table_size_ == 0
    ? nullptr
    : std::bit_cast<const char*>(
        data_.subspan(desc_.scene_strings.offset).data());

  if (desc_.script_slots.count != 0U) {
    const auto slots_size = uint64_t { desc_.script_slots.count }
      * sizeof(pak::scripting::ScriptSlotRecord);
    if (desc_.script_slots.entry_size
        != sizeof(pak::scripting::ScriptSlotRecord)
      || desc_.script_slots.offset < payload_end
      || !range_ok(desc_.script_slots.offset, slots_size, data_.size())) {
      throw std::runtime_error("SceneAsset script slot table is invalid");
    }
    payload_end = desc_.script_slots.offset + slots_size;
    for (const auto& slot : ReadScriptSlots(0U, desc_.script_slots.count)) {
      if (slot.script_asset_key.IsNil()) {
        throw std::runtime_error("Scene script slot has no script asset");
      }
      const auto params_size = uint64_t { slot.params_count }
        * sizeof(pak::scripting::ScriptParamRecord);
      if (params_size == 0U) {
        if (slot.params_array_offset != 0U) {
          throw std::runtime_error(
            "Empty scene script parameters must have zero offset");
        }
        continue;
      }
      if (slot.params_array_offset != payload_end
        || !range_ok(slot.params_array_offset, params_size, data_.size())) {
        throw std::runtime_error("SceneAsset script parameters must follow "
                                 "slots without gaps or overlap");
      }
      payload_end += params_size;
    }
  } else if (desc_.script_slots.offset != 0U
    || desc_.script_slots.entry_size != 0U) {
    throw std::runtime_error(
      "Empty scene script table must use a canonical empty range");
  }

  std::vector<std::pair<uint32_t, uint32_t>> script_ranges;
  for (const auto& component :
    GetComponents<pak::scripting::ScriptingComponentRecord>()) {
    if (component.slot_start_index > desc_.script_slots.count
      || component.slot_count
        > desc_.script_slots.count - component.slot_start_index) {
      throw std::runtime_error(
        "Scene scripting component slot range is out of bounds");
    }
    if (component.slot_count != 0U) {
      script_ranges.emplace_back(component.slot_start_index,
        component.slot_start_index + component.slot_count);
    }
  }
  std::ranges::sort(script_ranges);
  uint32_t next_script_slot = 0;
  for (const auto [begin, end] : script_ranges) {
    if (begin != next_script_slot) {
      throw std::runtime_error(
        "Scene script slot ownership has gaps or overlap");
    }
    next_script_slot = end;
  }
  if (next_script_slot != desc_.script_slots.count) {
    throw std::runtime_error("Scene script table contains unowned slots");
  }

  // Optional trailing environment block (v3+ scenes).
  // This block is not referenced by offsets in the descriptor; it begins at
  // the end of the scene payload.
  if (payload_end + sizeof(pak::world::SceneEnvironmentBlockHeader)
    <= data_.size()) {
    const auto env_header
      = ReadPackedRecord<pak::world::SceneEnvironmentBlockHeader>(
        data_.subspan(
          payload_end, sizeof(pak::world::SceneEnvironmentBlockHeader)),
        "SceneAsset environment block header");

    if (env_header.byte_size
      < sizeof(pak::world::SceneEnvironmentBlockHeader)) {
      throw std::runtime_error(
        "SceneAsset environment block byte_size too small");
    }

    const size_t env_end = payload_end + env_header.byte_size;
    if (env_end != data_.size() || env_end < payload_end) {
      throw std::runtime_error("SceneAsset environment block out of bounds");
    }

    has_environment_block_ = true;
    environment_block_header_ = env_header;

    environment_system_records_.reserve(env_header.systems_count);

    size_t cursor
      = payload_end + sizeof(pak::world::SceneEnvironmentBlockHeader);
    for (uint32_t i = 0; i < env_header.systems_count; ++i) {
      if (cursor + sizeof(pak::world::SceneEnvironmentSystemRecordHeader)
        > env_end) {
        throw std::runtime_error(
          "SceneAsset environment record header out of bounds");
      }

      const auto record_header
        = ReadPackedRecord<pak::world::SceneEnvironmentSystemRecordHeader>(
          data_.subspan(
            cursor, sizeof(pak::world::SceneEnvironmentSystemRecordHeader)),
          "SceneAsset environment record header");

      const uint32_t record_type = record_header.system_type;
      const uint32_t record_size = record_header.record_size;

      if (record_size
        < sizeof(pak::world::SceneEnvironmentSystemRecordHeader)) {
        throw std::runtime_error(
          "SceneAsset environment record_size too small");
      }

      const size_t record_end = cursor + record_size;
      if (record_end > env_end || record_end < cursor) {
        throw std::runtime_error("SceneAsset environment record out of bounds");
      }

      if (!pak::world::IsValidEnvironmentRecordSize(record_type, record_size)) {
        throw std::runtime_error("SceneAsset environment record size mismatch");
      }

      const auto bytes = data_.subspan(cursor, record_size);
      if (record_type
        == nostd::to_underlying(
          pak::world::EnvironmentComponentType::kPostProcessVolume)) {
        if (post_process_record_) {
          throw std::runtime_error(
            "SceneAsset contains duplicate post-process records");
        }
        const auto record
          = ReadPackedRecord<pak::world::PostProcessVolumeEnvironmentRecord>(
            bytes.first(sizeof(pak::world::PostProcessVolumeEnvironmentRecord)),
            "SceneAsset post-process record");
        auto key_bytes = bytes.subspan(sizeof(record));
        post_process_curve_.reserve(record.curve_key_count);
        for (uint32_t index = 0; index < record.curve_key_count; ++index) {
          const auto key
            = ReadPackedRecord<pak::world::ExposureCompensationKeyRecord>(
              key_bytes.first(
                sizeof(pak::world::ExposureCompensationKeyRecord)),
              "SceneAsset exposure compensation key");
          if (!post_process_curve_.empty()
            && key.metered_ev <= post_process_curve_.back().metered_ev) {
            throw std::runtime_error(
              "SceneAsset exposure curve EV keys must be strictly increasing");
          }
          post_process_curve_.push_back(key);
          key_bytes = key_bytes.subspan(sizeof(key));
        }
        post_process_record_ = record;
      }
      environment_system_records_.push_back(EnvironmentSystemRecordView {
        .header = record_header,
        .record_offset = cursor,
        .bytes = bytes,
      });
      cursor = record_end;
    }

    if (cursor != env_end) {
      throw std::runtime_error(
        "SceneAsset environment block contains trailing bytes");
    }
  }
  std::set<std::pair<pak::world::SceneNodeIndexT, MaterialSlotId>> assignments;
  std::unordered_set<pak::world::SceneNodeIndexT> renderable_nodes;
  for (const auto& renderable : GetComponents<pak::world::RenderableRecord>()) {
    renderable_nodes.insert(renderable.node_index);
  }
  for (const auto& assignment :
    GetComponents<pak::world::MaterialOverrideRecord>()) {
    if (!renderable_nodes.contains(assignment.node_index)
      || assignment.slot_id.IsNil() || assignment.material_key.IsNil()
      || base::IsAllZero(assignment.layout_revision)
      || !assignments.emplace(assignment.node_index, assignment.slot_id)
        .second) {
      throw std::runtime_error(
        "SceneAsset contains an invalid material assignment");
    }
  }
}

template <typename RecordT>
auto SceneAsset::TryGetEnvironmentRecordAs(
  const pak::world::EnvironmentComponentType type) const
  -> std::optional<RecordT>
{
  if (!HasEnvironmentBlock()) {
    return std::nullopt;
  }

  const auto records = GetEnvironmentSystemRecords();
  for (const auto& record : records) {
    if (record.header.system_type != nostd::to_underlying(type)) {
      continue;
    }

    if (record.bytes.size() != sizeof(RecordT)) {
      throw std::runtime_error(
        "SceneAsset environment record size mismatch (validated by loader)");
    }

    std::vector<std::byte> buffer;
    buffer.assign(record.bytes.begin(), record.bytes.end());

    oxygen::serio::MemoryStream stream { std::span<std::byte>(buffer) };
    oxygen::serio::Reader<oxygen::serio::MemoryStream> reader(stream);
    auto packed = reader.ScopedAlignment(1);

    RecordT decoded {};
    const auto res = reader.ReadInto(decoded);
    if (!res) {
      throw std::runtime_error(
        "SceneAsset failed to deserialize environment record");
    }
    return decoded;
  }

  return std::nullopt;
}

auto SceneAsset::TryGetSkyAtmosphereEnvironment() const
  -> std::optional<pak::world::SkyAtmosphereEnvironmentRecord>
{
  return TryGetEnvironmentRecordAs<pak::world::SkyAtmosphereEnvironmentRecord>(
    pak::world::EnvironmentComponentType::kSkyAtmosphere);
}

auto SceneAsset::TryGetVolumetricCloudsEnvironment() const
  -> std::optional<pak::world::VolumetricCloudsEnvironmentRecord>
{
  return TryGetEnvironmentRecordAs<
    pak::world::VolumetricCloudsEnvironmentRecord>(
    pak::world::EnvironmentComponentType::kVolumetricClouds);
}

auto SceneAsset::TryGetFogEnvironment() const
  -> std::optional<pak::world::FogEnvironmentRecord>
{
  return TryGetEnvironmentRecordAs<pak::world::FogEnvironmentRecord>(
    pak::world::EnvironmentComponentType::kFog);
}

auto SceneAsset::TryGetSkyLightEnvironment() const
  -> std::optional<pak::world::SkyLightEnvironmentRecord>
{
  if (!HasEnvironmentBlock()) {
    return std::nullopt;
  }

  const auto records = GetEnvironmentSystemRecords();
  for (const auto& record : records) {
    if (record.header.system_type
      != nostd::to_underlying(
        pak::world::EnvironmentComponentType::kSkyLight)) {
      continue;
    }

    if (record.bytes.size() == sizeof(pak::world::SkyLightEnvironmentRecord)) {
      return ReadPackedRecord<pak::world::SkyLightEnvironmentRecord>(
        record.bytes, "SceneAsset SkyLight environment record");
    }
    throw std::runtime_error(
      "SceneAsset SkyLight environment record size mismatch");
  }

  return std::nullopt;
}

auto SceneAsset::TryGetSkySphereEnvironment() const
  -> std::optional<pak::world::SkySphereEnvironmentRecord>
{
  return TryGetEnvironmentRecordAs<pak::world::SkySphereEnvironmentRecord>(
    pak::world::EnvironmentComponentType::kSkySphere);
}

auto SceneAsset::TryGetPostProcessVolumeEnvironment() const
  -> std::optional<pak::world::PostProcessVolumeEnvironmentRecord>
{
  return post_process_record_;
}

auto SceneAsset::TryGetBackgroundEnvironment() const
  -> std::optional<pak::world::BackgroundEnvironmentRecord>
{
  return TryGetEnvironmentRecordAs<pak::world::BackgroundEnvironmentRecord>(
    pak::world::EnvironmentComponentType::kBackground);
}

} // namespace oxygen::data
