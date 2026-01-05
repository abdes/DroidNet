//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Data/SceneAsset.h>

#include <limits>

#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::data {

SceneAsset::SceneAsset(AssetKey key, std::span<const std::byte> data)
  : Asset(key)
  , data_(data)
{
  ParseAndValidate();
}

SceneAsset::SceneAsset(AssetKey key, std::vector<std::byte> data)
  : Asset(key)
  , owned_data_(std::make_shared<std::vector<std::byte>>(std::move(data)))
  , data_(owned_data_->data(), owned_data_->size())
{
  ParseAndValidate();
}

auto SceneAsset::GetNodes() const noexcept -> std::span<const pak::NodeRecord>
{
  if (node_count_ == 0) {
    return {};
  }

  if (!nodes_cache_valid_) {
    const size_t nodes_bytes = node_count_ * sizeof(pak::NodeRecord);
    const auto bytes = data_.subspan(desc_.nodes.offset, nodes_bytes);

    std::vector<std::byte> buffer;
    buffer.assign(bytes.begin(), bytes.end());

    oxygen::serio::MemoryStream stream { std::span<std::byte>(buffer) };
    oxygen::serio::Reader<oxygen::serio::MemoryStream> reader(stream);
    auto pack = reader.ScopedAlignment(1);

    nodes_cache_.clear();
    nodes_cache_.resize(node_count_);
    for (size_t i = 0; i < node_count_; ++i) {
      const auto res = reader.ReadInto(nodes_cache_[i]);
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

auto SceneAsset::GetNode(pak::SceneNodeIndexT index) const noexcept
  -> const pak::NodeRecord&
{
  const auto nodes = GetNodes();
  DCHECK_LT_F(index, nodes.size());
  return nodes[index];
}

auto SceneAsset::GetNodeName(const pak::NodeRecord& node) const noexcept
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

auto SceneAsset::GetRootNode() const noexcept -> const pak::NodeRecord&
{
  const auto nodes = GetNodes();
  DCHECK_GT_F(nodes.size(), 0);
  return nodes[0];
}

auto SceneAsset::ParseAndValidate() -> void
{
  if (data_.size() < sizeof(pak::SceneAssetDesc)) {
    throw std::runtime_error("SceneAsset data too small for header");
  }

  std::memcpy(&desc_, data_.data(), sizeof(pak::SceneAssetDesc));

  auto range_ok
    = [](const size_t offset, const size_t size, const size_t total) {
        return offset <= total && size <= (total - offset);
      };

  has_environment_block_ = false;
  environment_system_records_.clear();

  size_t payload_end = sizeof(pak::SceneAssetDesc);

  // Validate Node Table
  if (desc_.nodes.count > 0) {
    const size_t nodes_bytes
      = static_cast<size_t>(desc_.nodes.count) * sizeof(pak::NodeRecord);
    if (!range_ok(desc_.nodes.offset, nodes_bytes, data_.size())) {
      throw std::runtime_error("SceneAsset node table out of bounds");
    }
    if (desc_.nodes.entry_size != sizeof(pak::NodeRecord)) {
      throw std::runtime_error("SceneAsset node record size mismatch");
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
    if (!bytes.empty() && bytes[0] != std::byte { 0 }) {
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
      * sizeof(pak::SceneComponentTableDesc);
    if (!range_ok(
          desc_.component_table_directory_offset, dir_bytes, data_.size())) {
      throw std::runtime_error("SceneAsset component directory out of bounds");
    }

    payload_end = (std::max)(payload_end,
      static_cast<size_t>(desc_.component_table_directory_offset + dir_bytes));

    const auto dir_span
      = data_.subspan(desc_.component_table_directory_offset, dir_bytes);

    component_tables_.clear();
    component_tables_.reserve(desc_.component_table_count);
    for (uint32_t i = 0; i < desc_.component_table_count; ++i) {
      const auto entry_bytes = dir_span.subspan(
        static_cast<size_t>(i) * sizeof(pak::SceneComponentTableDesc),
        sizeof(pak::SceneComponentTableDesc));
      pak::SceneComponentTableDesc entry {};
      std::memcpy(&entry, entry_bytes.data(), sizeof(entry));

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
        && entry.table.entry_size != sizeof(pak::RenderableRecord)) {
        throw std::runtime_error("SceneAsset renderable record size mismatch");
      }
      if (type == ComponentType::kPerspectiveCamera
        && entry.table.entry_size != sizeof(pak::PerspectiveCameraRecord)) {
        throw std::runtime_error(
          "SceneAsset perspective camera record size mismatch");
      }
      if (type == ComponentType::kOrthographicCamera
        && entry.table.entry_size != sizeof(pak::OrthographicCameraRecord)) {
        throw std::runtime_error(
          "SceneAsset orthographic camera record size mismatch");
      }

      if (type == ComponentType::kDirectionalLight
        && entry.table.entry_size != sizeof(pak::DirectionalLightRecord)) {
        throw std::runtime_error(
          "SceneAsset directional light record size mismatch");
      }
      if (type == ComponentType::kPointLight
        && entry.table.entry_size != sizeof(pak::PointLightRecord)) {
        throw std::runtime_error("SceneAsset point light record size mismatch");
      }
      if (type == ComponentType::kSpotLight
        && entry.table.entry_size != sizeof(pak::SpotLightRecord)) {
        throw std::runtime_error("SceneAsset spot light record size mismatch");
      }

      component_tables_.push_back({ .type = type,
        .offset = entry.table.offset,
        .count = entry.table.count,
        .entry_size = entry.table.entry_size });
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

  // Optional trailing environment block (v3+ scenes).
  // This block is not referenced by offsets in the descriptor; it begins at
  // the end of the scene payload.
  if (payload_end + sizeof(pak::SceneEnvironmentBlockHeader) <= data_.size()) {
    pak::SceneEnvironmentBlockHeader env_header {};
    std::memcpy(&env_header,
      data_.subspan(payload_end, sizeof(env_header)).data(),
      sizeof(env_header));

    if (env_header.byte_size < sizeof(pak::SceneEnvironmentBlockHeader)) {
      throw std::runtime_error(
        "SceneAsset environment block byte_size too small");
    }

    const size_t env_end = payload_end + env_header.byte_size;
    if (env_end > data_.size() || env_end < payload_end) {
      throw std::runtime_error("SceneAsset environment block out of bounds");
    }

    has_environment_block_ = true;
    environment_block_header_ = env_header;

    environment_system_records_.reserve(env_header.systems_count);

    size_t cursor = payload_end + sizeof(pak::SceneEnvironmentBlockHeader);
    for (uint32_t i = 0; i < env_header.systems_count; ++i) {
      if (cursor + sizeof(pak::SceneEnvironmentSystemRecordHeader) > env_end) {
        throw std::runtime_error(
          "SceneAsset environment record header out of bounds");
      }

      pak::SceneEnvironmentSystemRecordHeader record_header {};
      std::memcpy(&record_header,
        data_.subspan(cursor, sizeof(record_header)).data(),
        sizeof(record_header));

      if (record_header.record_size
        < sizeof(pak::SceneEnvironmentSystemRecordHeader)) {
        throw std::runtime_error(
          "SceneAsset environment record_size too small");
      }

      const size_t record_end = cursor + record_header.record_size;
      if (record_end > env_end || record_end < cursor) {
        throw std::runtime_error("SceneAsset environment record out of bounds");
      }

      // Known record types must match their packed sizes.
      const auto type
        = static_cast<pak::EnvironmentComponentType>(record_header.system_type);
      switch (type) {
      case pak::EnvironmentComponentType::kSkyAtmosphere:
        if (record_header.record_size
          != sizeof(pak::SkyAtmosphereEnvironmentRecord)) {
          throw std::runtime_error(
            "SceneAsset SkyAtmosphere record size mismatch");
        }
        break;
      case pak::EnvironmentComponentType::kVolumetricClouds:
        if (record_header.record_size
          != sizeof(pak::VolumetricCloudsEnvironmentRecord)) {
          throw std::runtime_error(
            "SceneAsset VolumetricClouds record size mismatch");
        }
        break;
      case pak::EnvironmentComponentType::kSkyLight:
        if (record_header.record_size
          != sizeof(pak::SkyLightEnvironmentRecord)) {
          throw std::runtime_error("SceneAsset SkyLight record size mismatch");
        }
        break;
      case pak::EnvironmentComponentType::kSkySphere:
        if (record_header.record_size
          != sizeof(pak::SkySphereEnvironmentRecord)) {
          throw std::runtime_error("SceneAsset SkySphere record size mismatch");
        }
        break;
      case pak::EnvironmentComponentType::kPostProcessVolume:
        if (record_header.record_size
          != sizeof(pak::PostProcessVolumeEnvironmentRecord)) {
          throw std::runtime_error(
            "SceneAsset PostProcessVolume record size mismatch");
        }
        break;
      default:
        // Unknown types are permitted; they are skipped via record_size.
        break;
      }

      const auto bytes = data_.subspan(cursor, record_header.record_size);
      environment_system_records_.push_back(EnvironmentSystemRecordView {
        .header = record_header, .bytes = bytes });
      cursor = record_end;
    }

    if (cursor != env_end) {
      throw std::runtime_error(
        "SceneAsset environment block contains trailing bytes");
    }
  }
}

template <typename RecordT>
auto SceneAsset::TryGetEnvironmentRecordAs(
  const pak::EnvironmentComponentType type) const -> std::optional<RecordT>
{
  if (!HasEnvironmentBlock()) {
    return std::nullopt;
  }

  const auto records = GetEnvironmentSystemRecords();
  for (const auto& record : records) {
    if (record.header.system_type != static_cast<uint32_t>(type)) {
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
  -> std::optional<pak::SkyAtmosphereEnvironmentRecord>
{
  return TryGetEnvironmentRecordAs<pak::SkyAtmosphereEnvironmentRecord>(
    pak::EnvironmentComponentType::kSkyAtmosphere);
}

auto SceneAsset::TryGetVolumetricCloudsEnvironment() const
  -> std::optional<pak::VolumetricCloudsEnvironmentRecord>
{
  return TryGetEnvironmentRecordAs<pak::VolumetricCloudsEnvironmentRecord>(
    pak::EnvironmentComponentType::kVolumetricClouds);
}

auto SceneAsset::TryGetSkyLightEnvironment() const
  -> std::optional<pak::SkyLightEnvironmentRecord>
{
  return TryGetEnvironmentRecordAs<pak::SkyLightEnvironmentRecord>(
    pak::EnvironmentComponentType::kSkyLight);
}

auto SceneAsset::TryGetSkySphereEnvironment() const
  -> std::optional<pak::SkySphereEnvironmentRecord>
{
  return TryGetEnvironmentRecordAs<pak::SkySphereEnvironmentRecord>(
    pak::EnvironmentComponentType::kSkySphere);
}

auto SceneAsset::TryGetPostProcessVolumeEnvironment() const
  -> std::optional<pak::PostProcessVolumeEnvironmentRecord>
{
  return TryGetEnvironmentRecordAs<pak::PostProcessVolumeEnvironmentRecord>(
    pak::EnvironmentComponentType::kPostProcessVolume);
}

} // namespace oxygen::data
