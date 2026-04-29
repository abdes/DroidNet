//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>

#include <string>
#include <string_view>

namespace oxygen::data {

namespace {

  template <typename RecordT>
  auto ReadPackedRecord(const std::span<const std::byte> bytes,
    const std::string_view what) -> RecordT
  {
    std::vector<std::byte> buffer;
    buffer.assign(bytes.begin(), bytes.end());

    oxygen::serio::MemoryStream stream { std::span<std::byte>(buffer) };
    oxygen::serio::Reader<oxygen::serio::MemoryStream> reader(stream);
    auto packed = reader.ScopedAlignment(1);

    RecordT record {};
    const auto res = reader.ReadInto(record);
    if (!res) {
      throw std::runtime_error(std::string(what) + " decode failed");
    }
    return record;
  }

  auto IsExpectedEnvironmentRecordSize(
    const uint32_t record_type, const uint32_t record_size) noexcept -> bool
  {
    const auto expected_size
      = pak::world::ExpectedEnvironmentRecordSize(record_type);
    return !expected_size.has_value() || record_size == *expected_size;
  }

} // namespace

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

auto SceneAsset::GetNode(pak::world::SceneNodeIndexT index) const noexcept
  -> const pak::world::NodeRecord&
{
  const auto nodes = GetNodes();
  DCHECK_LT_F(index, nodes.size());
  return nodes[index];
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
  return nodes[0];
}

auto SceneAsset::ParseAndValidate() -> void
{
  if (data_.size() < sizeof(pak::world::SceneAssetDesc)) {
    throw std::runtime_error("SceneAsset data too small for header");
  }

  desc_ = ReadPackedRecord<pak::world::SceneAssetDesc>(
    data_.first(sizeof(pak::world::SceneAssetDesc)), "SceneAsset header");

  if (desc_.header.version != pak::world::kSceneAssetVersion) {
    throw std::runtime_error("SceneAsset unsupported descriptor version");
  }

  auto range_ok
    = [](const size_t offset, const size_t size, const size_t total) {
        return offset <= total && size <= (total - offset);
      };

  has_environment_block_ = false;
  environment_system_records_.clear();

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
      * sizeof(pak::world::SceneComponentTableDesc);
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
    if (env_end > data_.size() || env_end < payload_end) {
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

      if (!IsExpectedEnvironmentRecordSize(record_type, record_size)) {
        throw std::runtime_error("SceneAsset environment record size mismatch");
      }

      const auto bytes = data_.subspan(cursor, record_size);
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
  return TryGetEnvironmentRecordAs<
    pak::world::PostProcessVolumeEnvironmentRecord>(
    pak::world::EnvironmentComponentType::kPostProcessVolume);
}

} // namespace oxygen::data
