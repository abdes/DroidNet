//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::loaders {

namespace detail {

  inline auto CheckResult(const oxygen::Result<void>& result, const char* field)
    -> void
  {
    if (!result) {
      LOG_F(
        ERROR, "-failed- on {}: {}", field, result.error().message().c_str());
      throw std::runtime_error(fmt::format(
        "error reading scene asset ({}): {}", field, result.error().message()));
    }
  }

  template <typename T>
  inline auto CheckResult(const oxygen::Result<T>& result, const char* field)
    -> void
  {
    if (!result) {
      LOG_F(
        ERROR, "-failed- on {}: {}", field, result.error().message().c_str());
      throw std::runtime_error(fmt::format(
        "error reading scene asset ({}): {}", field, result.error().message()));
    }
  }

  inline auto AddRangeEnd(size_t& end, const size_t offset, const size_t size)
    -> void
  {
    if (size == 0) {
      end = std::max(end, offset);
      return;
    }

    const size_t candidate = offset + size;
    if (candidate < offset) {
      throw std::runtime_error("scene asset range overflow");
    }
    end = std::max(end, candidate);
  }

  inline auto ValidateStringOffset(
    const std::span<const std::byte> string_table,
    const oxygen::data::pak::StringTableOffsetT offset) -> void
  {
    if (offset >= string_table.size()) {
      throw std::runtime_error("scene asset node name offset out of bounds");
    }

    const auto tail = string_table.subspan(offset);
    const auto it = std::find(tail.begin(), tail.end(), std::byte { 0 });
    if (it == tail.end()) {
      throw std::runtime_error(
        "scene asset string table entry missing NUL terminator");
    }
  }

  template <typename RecordT>
  inline auto ValidateComponentTable(
    const std::span<const std::byte> table_bytes, const uint32_t count,
    const uint32_t entry_size, const uint32_t node_count) -> void
  {
    if (count == 0) {
      return;
    }
    if (entry_size != sizeof(RecordT)) {
      throw std::runtime_error("scene asset component record size mismatch");
    }
    if (table_bytes.size() < static_cast<size_t>(count) * sizeof(RecordT)) {
      throw std::runtime_error("scene asset component table out of bounds");
    }

    oxygen::data::pak::SceneNodeIndexT prev = 0;
    bool have_prev = false;
    for (uint32_t i = 0; i < count; ++i) {
      RecordT record {};
      std::memcpy(&record,
        table_bytes
          .subspan(static_cast<size_t>(i) * sizeof(RecordT), sizeof(RecordT))
          .data(),
        sizeof(RecordT));

      if (record.node_index >= node_count) {
        throw std::runtime_error(
          "scene asset component node_index out of range");
      }

      if (have_prev && record.node_index < prev) {
        throw std::runtime_error(
          "scene asset component table must be sorted by node_index");
      }
      prev = record.node_index;
      have_prev = true;
    }
  }

  inline auto ValidateTrailingEnvironmentBlock(
    const std::span<const std::byte> bytes, const size_t payload_end) -> void
  {
    if (payload_end > bytes.size()) {
      throw std::runtime_error("scene asset payload end out of bounds");
    }

    if (payload_end + sizeof(oxygen::data::pak::SceneEnvironmentBlockHeader)
      > bytes.size()) {
      return;
    }

    oxygen::data::pak::SceneEnvironmentBlockHeader header {};
    std::memcpy(&header,
      bytes
        .subspan(
          payload_end, sizeof(oxygen::data::pak::SceneEnvironmentBlockHeader))
        .data(),
      sizeof(header));

    if (header.byte_size
      < sizeof(oxygen::data::pak::SceneEnvironmentBlockHeader)) {
      throw std::runtime_error("scene environment block byte_size too small");
    }

    const size_t env_end = payload_end + header.byte_size;
    if (env_end > bytes.size() || env_end < payload_end) {
      throw std::runtime_error("scene environment block out of bounds");
    }

    size_t cursor
      = payload_end + sizeof(oxygen::data::pak::SceneEnvironmentBlockHeader);
    for (uint32_t i = 0; i < header.systems_count; ++i) {
      if (cursor + sizeof(oxygen::data::pak::SceneEnvironmentSystemRecordHeader)
        > env_end) {
        throw std::runtime_error(
          "scene environment record header out of bounds");
      }

      oxygen::data::pak::SceneEnvironmentSystemRecordHeader record_header {};
      std::memcpy(&record_header,
        bytes
          .subspan(cursor,
            sizeof(oxygen::data::pak::SceneEnvironmentSystemRecordHeader))
          .data(),
        sizeof(record_header));

      if (record_header.record_size
        < sizeof(oxygen::data::pak::SceneEnvironmentSystemRecordHeader)) {
        throw std::runtime_error("scene environment record_size too small");
      }

      const size_t record_end = cursor + record_header.record_size;
      if (record_end > env_end || record_end < cursor) {
        throw std::runtime_error("scene environment record out of bounds");
      }

      const auto type
        = static_cast<oxygen::data::pak::EnvironmentComponentType>(
          record_header.system_type);
      switch (type) {
      case oxygen::data::pak::EnvironmentComponentType::kSkyAtmosphere:
        if (record_header.record_size
          != sizeof(oxygen::data::pak::SkyAtmosphereEnvironmentRecord)) {
          throw std::runtime_error(
            "scene environment SkyAtmosphere record size mismatch");
        }
        break;
      case oxygen::data::pak::EnvironmentComponentType::kVolumetricClouds:
        if (record_header.record_size
          != sizeof(oxygen::data::pak::VolumetricCloudsEnvironmentRecord)) {
          throw std::runtime_error(
            "scene environment VolumetricClouds record size mismatch");
        }
        break;
      case oxygen::data::pak::EnvironmentComponentType::kSkyLight:
        if (record_header.record_size
          != sizeof(oxygen::data::pak::SkyLightEnvironmentRecord)) {
          throw std::runtime_error(
            "scene environment SkyLight record size mismatch");
        }
        break;
      case oxygen::data::pak::EnvironmentComponentType::kSkySphere:
        if (record_header.record_size
          != sizeof(oxygen::data::pak::SkySphereEnvironmentRecord)) {
          throw std::runtime_error(
            "scene environment SkySphere record size mismatch");
        }
        break;
      case oxygen::data::pak::EnvironmentComponentType::kPostProcessVolume:
        if (record_header.record_size
          != sizeof(oxygen::data::pak::PostProcessVolumeEnvironmentRecord)) {
          throw std::runtime_error(
            "scene environment PostProcessVolume record size mismatch");
        }
        break;
      default:
        break;
      }

      cursor = record_end;
    }

    if (cursor != env_end) {
      throw std::runtime_error("scene environment block has trailing bytes");
    }
  }

} // namespace detail

//! Loader for scene assets.
inline auto LoadSceneAsset(LoaderContext context)
  -> std::unique_ptr<data::SceneAsset>
{
  LOG_SCOPE_FUNCTION(INFO);
  LOG_F(2, "offline mode   : {}", context.work_offline ? "yes" : "no");

  DCHECK_NOTNULL_F(context.desc_reader, "expecting desc_reader not to be null");
  auto& reader = *context.desc_reader;

  // Scene descriptors are packed byte blobs.
  // Use scoped alignment 1 to avoid any implicit alignment/padding behavior.
  auto packed = reader.ScopedAlignment(1);

  const auto base_pos_res = reader.Position();
  detail::CheckResult(base_pos_res, "Position(base)");
  const size_t base_pos = *base_pos_res;

  // Read the fixed header first so we can compute the total descriptor size.
  data::pak::SceneAssetDesc desc {};
  {
    auto blob_res = reader.ReadBlob(sizeof(desc));
    detail::CheckResult(blob_res, "ReadBlob(SceneAssetDesc)");
    std::memcpy(&desc, (*blob_res).data(), sizeof(desc));
  }

  if (static_cast<data::AssetType>(desc.header.asset_type)
    != data::AssetType::kScene) {
    throw std::runtime_error("invalid asset type for scene descriptor");
  }

  // Scene descriptor format versioning is per-asset (AssetHeader::version),
  // independent from the PAK container format version.
  // v2: no trailing SceneEnvironment block.
  // v3: trailing SceneEnvironment block is required (empty allowed).
  const bool expects_environment_block
    = desc.header.version >= oxygen::data::pak::v3::kSceneAssetVersion;

  // Compute the full payload size from the descriptor ranges.
  size_t end = sizeof(data::pak::SceneAssetDesc);

  if (desc.nodes.count > 0) {
    if (desc.nodes.entry_size != sizeof(data::pak::NodeRecord)) {
      throw std::runtime_error("scene asset node record size mismatch");
    }
    const size_t bytes
      = static_cast<size_t>(desc.nodes.count) * desc.nodes.entry_size;
    detail::AddRangeEnd(end, desc.nodes.offset, bytes);
  }

  detail::AddRangeEnd(end, desc.scene_strings.offset, desc.scene_strings.size);

  // Read the component directory entries (if any) to validate and extend end.
  std::vector<data::pak::SceneComponentTableDesc> tables;
  if (desc.component_table_count > 0) {
    const size_t dir_bytes = static_cast<size_t>(desc.component_table_count)
      * sizeof(data::pak::SceneComponentTableDesc);
    detail::AddRangeEnd(end, desc.component_table_directory_offset, dir_bytes);

    auto seek_res
      = reader.Seek(base_pos + desc.component_table_directory_offset);
    detail::CheckResult(seek_res, "Seek(component_table_directory)");

    tables.reserve(desc.component_table_count);
    for (uint32_t i = 0; i < desc.component_table_count; ++i) {
      data::pak::SceneComponentTableDesc entry {};
      auto entry_blob = reader.ReadBlob(sizeof(entry));
      detail::CheckResult(entry_blob, "ReadBlob(SceneComponentTableDesc)");
      std::memcpy(&entry, (*entry_blob).data(), sizeof(entry));

      if (entry.table.count > 0) {
        const size_t bytes
          = static_cast<size_t>(entry.table.count) * entry.table.entry_size;
        detail::AddRangeEnd(end, entry.table.offset, bytes);
      }

      tables.push_back(entry);
    }
  }

  // Load the full descriptor payload as bytes.
  {
    auto seek_res = reader.Seek(base_pos);
    detail::CheckResult(seek_res, "Seek(base)");
  }

  auto blob_res = reader.ReadBlob(end);
  detail::CheckResult(blob_res, "ReadBlob(scene_payload)");
  std::vector<std::byte> bytes = std::move(*blob_res);

  const size_t payload_end = end;
  if (expects_environment_block) {
    data::pak::SceneEnvironmentBlockHeader env_header {};
    const auto header_res = reader.ReadBlob(sizeof(env_header));
    detail::CheckResult(header_res, "ReadBlob(scene_environment_header)");
    std::memcpy(&env_header, (*header_res).data(), sizeof(env_header));
    if (env_header.byte_size < sizeof(data::pak::SceneEnvironmentBlockHeader)) {
      throw std::runtime_error("scene environment block byte_size too small");
    }

    const size_t tail_size
      = env_header.byte_size - sizeof(data::pak::SceneEnvironmentBlockHeader);
    const auto tail_res = reader.ReadBlob(tail_size);
    detail::CheckResult(tail_res, "ReadBlob(scene_environment_block)");

    bytes.reserve(bytes.size() + sizeof(env_header) + tail_size);
    bytes.insert(bytes.end(), (*header_res).begin(), (*header_res).end());
    bytes.insert(bytes.end(), (*tail_res).begin(), (*tail_res).end());
  }

  // Full validation (loader responsibility).
  const auto bytes_span = std::span<const std::byte>(bytes);

  if (desc.scene_strings.size > 0) {
    const auto table
      = bytes_span.subspan(desc.scene_strings.offset, desc.scene_strings.size);
    if (!table.empty() && table.front() != std::byte { 0 }) {
      throw std::runtime_error("scene asset string table must start with NUL");
    }

    if (desc.nodes.count > 0) {
      const auto nodes_bytes = bytes_span.subspan(desc.nodes.offset,
        static_cast<size_t>(desc.nodes.count) * sizeof(data::pak::NodeRecord));
      for (uint32_t i = 0; i < desc.nodes.count; ++i) {
        data::pak::NodeRecord node {};
        std::memcpy(&node,
          nodes_bytes
            .subspan(static_cast<size_t>(i) * sizeof(node), sizeof(node))
            .data(),
          sizeof(node));

        if (node.parent_index >= desc.nodes.count) {
          throw std::runtime_error("scene asset parent_index out of range");
        }

        if (node.scene_name_offset != 0) {
          detail::ValidateStringOffset(table, node.scene_name_offset);
        }
      }
    }
  }

  // Validate known component tables and (optionally) collect dependencies.
  const uint32_t node_count = desc.nodes.count;
  std::unordered_set<oxygen::data::AssetKey> geometry_deps;

  for (const auto& entry : tables) {
    if (entry.table.count == 0) {
      continue;
    }

    const size_t table_bytes_total
      = static_cast<size_t>(entry.table.count) * entry.table.entry_size;
    const auto table_bytes
      = bytes_span.subspan(entry.table.offset, table_bytes_total);

    const auto type
      = static_cast<oxygen::data::ComponentType>(entry.component_type);

    if (type == oxygen::data::ComponentType::kRenderable) {
      detail::ValidateComponentTable<oxygen::data::pak::RenderableRecord>(
        table_bytes, entry.table.count, entry.table.entry_size, node_count);

      // Dependency collection is identity-only.
      for (uint32_t i = 0; i < entry.table.count; ++i) {
        oxygen::data::pak::RenderableRecord record {};
        std::memcpy(&record,
          table_bytes
            .subspan(static_cast<size_t>(i) * sizeof(record), sizeof(record))
            .data(),
          sizeof(record));
        geometry_deps.insert(record.geometry_key);
      }
    } else if (type == oxygen::data::ComponentType::kPerspectiveCamera) {
      detail::ValidateComponentTable<
        oxygen::data::pak::PerspectiveCameraRecord>(
        table_bytes, entry.table.count, entry.table.entry_size, node_count);
    } else if (type == oxygen::data::ComponentType::kOrthographicCamera) {
      detail::ValidateComponentTable<
        oxygen::data::pak::OrthographicCameraRecord>(
        table_bytes, entry.table.count, entry.table.entry_size, node_count);
    } else if (type == oxygen::data::ComponentType::kDirectionalLight) {
      detail::ValidateComponentTable<oxygen::data::pak::DirectionalLightRecord>(
        table_bytes, entry.table.count, entry.table.entry_size, node_count);
    } else if (type == oxygen::data::ComponentType::kPointLight) {
      detail::ValidateComponentTable<oxygen::data::pak::PointLightRecord>(
        table_bytes, entry.table.count, entry.table.entry_size, node_count);
    } else if (type == oxygen::data::ComponentType::kSpotLight) {
      detail::ValidateComponentTable<oxygen::data::pak::SpotLightRecord>(
        table_bytes, entry.table.count, entry.table.entry_size, node_count);
    }
  }

  detail::ValidateTrailingEnvironmentBlock(bytes_span, payload_end);

  if (!context.parse_only) {
    if (!context.dependency_collector) {
      LOG_F(ERROR,
        "SceneLoader requires a DependencyCollector for non-parse-only loads");
      throw std::runtime_error(
        "SceneLoader requires a DependencyCollector for async decode");
    }

    for (const auto& dep : geometry_deps) {
      context.dependency_collector->AddAssetDependency(dep);
    }
  }

  return std::make_unique<data::SceneAsset>(
    context.current_asset_key, std::move(bytes));
}

} // namespace oxygen::content::loaders
