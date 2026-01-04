//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/PakFormat.h>

#include "AssetDumpHelpers.h"
#include "AssetDumper.h"

namespace oxygen::content::pakdump {

//! Dumps scene asset descriptors.
class SceneAssetDumper final : public AssetDumper {
public:
  void Dump(const oxygen::content::PakFile& pak,
    const oxygen::data::pak::v2::AssetDirectoryEntry& entry, DumpContext& ctx,
    const size_t idx) const override
  {
    using oxygen::data::ComponentType;
    using oxygen::data::pak::NodeRecord;
    using oxygen::data::pak::OrthographicCameraRecord;
    using oxygen::data::pak::PerspectiveCameraRecord;
    using oxygen::data::pak::RenderableRecord;
    using oxygen::data::pak::SceneAssetDesc;
    using oxygen::data::pak::SceneComponentTableDesc;

    std::cout << "Asset #" << idx << ":\n";
    asset_dump_helpers::PrintAssetKey(entry.asset_key, ctx);
    asset_dump_helpers::PrintAssetMetadata(entry);

    const auto data = asset_dump_helpers::ReadDescriptorBytes(pak, entry);
    if (!data) {
      std::cout << "    Failed to read asset descriptor data\n\n";
      return;
    }

    asset_dump_helpers::PrintAssetDescriptorHexPreview(*data, ctx);
    if (data->size() < sizeof(SceneAssetDesc)) {
      std::cout << "    SceneAssetDesc: (insufficient data)\n\n";
      return;
    }

    SceneAssetDesc scene {};
    std::memcpy(&scene, data->data(), sizeof(scene));

    std::cout << "    --- Scene Descriptor Fields ---\n";
    asset_dump_helpers::PrintAssetHeaderFields(scene.header);

    PrintUtils::Field(
      "Nodes Offset", asset_dump_helpers::ToHexString(scene.nodes.offset), 8);
    PrintUtils::Field("Nodes Count", scene.nodes.count, 8);
    PrintUtils::Field("Nodes Entry Size", scene.nodes.entry_size, 8);
    PrintUtils::Field("Strings Offset",
      asset_dump_helpers::ToHexString(scene.scene_strings.offset), 8);
    PrintUtils::Field("Strings Size", scene.scene_strings.size, 8);
    PrintUtils::Field("Component Dir Offset",
      asset_dump_helpers::ToHexString(scene.component_table_directory_offset),
      8);
    PrintUtils::Field("Component Table Count", scene.component_table_count, 8);
    std::cout << "\n";

    const auto has_range = [&](const size_t offset, const size_t size) {
      return offset <= data->size() && size <= (data->size() - offset);
    };

    std::string_view string_table;
    if (scene.scene_strings.size > 0) {
      const size_t st_off = scene.scene_strings.offset;
      const size_t st_size = scene.scene_strings.size;
      if (has_range(st_off, st_size)) {
        string_table = std::string_view(
          reinterpret_cast<const char*>(data->data() + st_off), st_size);
      } else {
        std::cout
          << "    Scene string table: (not present in descriptor: offset="
          << asset_dump_helpers::ToHexString(st_off) << ", size=" << st_size
          << ", have " << data->size() << " bytes)\n\n";
      }
    }

    const auto TryGetNodeName
      = [&](const uint32_t node_index) -> std::string_view {
      if (node_index >= scene.nodes.count) {
        return {};
      }
      if (scene.nodes.entry_size != sizeof(NodeRecord)) {
        return {};
      }

      const uint64_t node_offset_u64 = scene.nodes.offset
        + static_cast<uint64_t>(node_index) * sizeof(NodeRecord);
      if (node_offset_u64 > (std::numeric_limits<size_t>::max)()) {
        return {};
      }

      NodeRecord node {};
      const auto node_offset = static_cast<size_t>(node_offset_u64);
      if (!asset_dump_helpers::ReadStructAt(
            *data, node_offset, sizeof(node), &node)) {
        return {};
      }
      return asset_dump_helpers::TryGetSceneString(
        string_table, node.scene_name_offset);
    };

    if (scene.nodes.count > 0) {
      const uint32_t node_limit
        = ctx.verbose ? scene.nodes.count : (std::min)(scene.nodes.count, 16u);

      if (scene.nodes.entry_size != sizeof(NodeRecord)) {
        std::cout << "    NodeRecord entry_size mismatch: expected "
                  << sizeof(NodeRecord) << ", got " << scene.nodes.entry_size
                  << "\n\n";
      } else {
        const uint64_t nodes_total_u64
          = static_cast<uint64_t>(scene.nodes.count)
          * static_cast<uint64_t>(scene.nodes.entry_size);
        const auto nodes_total = static_cast<size_t>(nodes_total_u64);
        const auto nodes_offset = static_cast<size_t>(scene.nodes.offset);
        if (!has_range(nodes_offset, nodes_total)) {
          std::cout << "    Nodes (" << scene.nodes.count
                    << "): (not present in descriptor: offset="
                    << asset_dump_helpers::ToHexString(nodes_offset)
                    << ", bytes=" << nodes_total << ", have " << data->size()
                    << ")\n\n";
        } else {
          std::vector<NodeRecord> nodes;
          nodes.resize(scene.nodes.count);
          std::memcpy(nodes.data(), data->data() + nodes_offset, nodes_total);

          std::cout << "    Nodes (" << scene.nodes.count << "):\n";
          for (uint32_t i = 0; i < node_limit; ++i) {
            const auto& node = nodes[i];
            const auto name = asset_dump_helpers::TryGetSceneString(
              string_table, node.scene_name_offset);

            std::cout << "      [" << i << "] "
                      << (name.empty() ? "(unnamed)" : std::string(name))
                      << " (parent=" << node.parent_index << ")\n";

            if (ctx.verbose) {
              PrintUtils::Field(
                "Node ID", oxygen::data::to_string(node.node_id), 10);
              PrintUtils::Field(
                "Flags", asset_dump_helpers::ToHexString(node.node_flags), 10);
              PrintUtils::Field(
                "T", asset_dump_helpers::FormatVec3(node.translation), 10);
              PrintUtils::Field(
                "R", asset_dump_helpers::FormatQuat(node.rotation), 10);
              PrintUtils::Field(
                "S", asset_dump_helpers::FormatVec3(node.scale), 10);
            }
          }

          if (scene.nodes.count > node_limit) {
            std::cout << "      ... (" << (scene.nodes.count - node_limit)
                      << " more nodes)\n";
          }

          // Print a hierarchical view (tree) to visualize parent/child
          // relationships at a glance.
          std::cout << "\n";
          std::cout << "    Node Hierarchy:\n";

          std::vector<std::vector<uint32_t>> children;
          children.resize(scene.nodes.count);

          std::vector<uint32_t> roots;
          roots.reserve(scene.nodes.count);

          for (uint32_t i = 0; i < scene.nodes.count; ++i) {
            const auto parent = nodes[i].parent_index;
            if (i == 0 || parent == i || parent >= scene.nodes.count) {
              roots.push_back(i);
              continue;
            }
            children[parent].push_back(i);
          }

          for (auto& c : children) {
            std::sort(c.begin(), c.end());
          }
          std::sort(roots.begin(), roots.end());

          std::vector<bool> visited;
          visited.resize(scene.nodes.count, false);

          const auto PrintSubtree = [&](auto&& self, const uint32_t node_index,
                                      const uint32_t depth) -> void {
            if (node_index >= nodes.size()) {
              return;
            }

            const auto name = asset_dump_helpers::TryGetSceneString(
              string_table, nodes[node_index].scene_name_offset);

            const auto indent = static_cast<int>(10 + depth * 2);
            const auto pretty_name
              = name.empty() ? "(unnamed)" : std::string(name);

            if (visited[node_index]) {
              fmt::print(
                "{:>{}}[{}] {} (cycle)\n", "", indent, node_index, pretty_name);
              return;
            }
            visited[node_index] = true;

            fmt::print("{:>{}}[{}] {}\n", "", indent, node_index, pretty_name);

            for (const auto child_index : children[node_index]) {
              self(self, child_index, depth + 1);
            }
          };

          for (const auto root_index : roots) {
            PrintSubtree(PrintSubtree, root_index, 0);
          }

          std::cout << "\n";
        }
      }
    }

    if (scene.component_table_count > 0) {
      const size_t dir_offset = scene.component_table_directory_offset;
      const size_t dir_size = static_cast<size_t>(scene.component_table_count)
        * sizeof(SceneComponentTableDesc);

      if (!has_range(dir_offset, dir_size)) {
        std::cout << "    Component table directory ("
                  << scene.component_table_count
                  << "): (not present in descriptor: offset="
                  << asset_dump_helpers::ToHexString(dir_offset)
                  << ", bytes=" << dir_size << ", have " << data->size()
                  << ")\n\n";
      } else {
        std::cout << "    Component Tables (" << scene.component_table_count
                  << "):\n";

        for (uint32_t i = 0; i < scene.component_table_count; ++i) {
          SceneComponentTableDesc comp_desc {};
          const size_t entry_off = dir_offset + i * sizeof(comp_desc);
          if (!asset_dump_helpers::ReadStructAt(
                *data, entry_off, sizeof(comp_desc), &comp_desc)) {
            break;
          }

          const auto component_type
            = static_cast<ComponentType>(comp_desc.component_type);

          std::cout << "      [" << i << "] "
                    << ::nostd::to_string(component_type) << " ("
                    << asset_dump_helpers::ToHexString(comp_desc.component_type)
                    << ")\n";
          PrintUtils::Field("Offset",
            asset_dump_helpers::ToHexString(comp_desc.table.offset), 10);
          PrintUtils::Field("Count", comp_desc.table.count, 10);
          PrintUtils::Field("Entry Size", comp_desc.table.entry_size, 10);

          if (component_type == ComponentType::kRenderable) {
            if (comp_desc.table.entry_size != sizeof(RenderableRecord)) {
              std::cout << "          RenderableRecord entry_size mismatch: "
                        << "expected " << sizeof(RenderableRecord) << ", got "
                        << comp_desc.table.entry_size << "\n";
              continue;
            }

            const uint64_t records_total_u64
              = static_cast<uint64_t>(comp_desc.table.count)
              * static_cast<uint64_t>(comp_desc.table.entry_size);
            if (records_total_u64 > (std::numeric_limits<size_t>::max)()) {
              std::cout << "          Renderables (" << comp_desc.table.count
                        << "): (too large to inspect)\n";
              continue;
            }
            if (comp_desc.table.offset > (std::numeric_limits<size_t>::max)()) {
              std::cout << "          Renderables (" << comp_desc.table.count
                        << "): (offset too large to inspect)\n";
              continue;
            }

            const auto records_total = static_cast<size_t>(records_total_u64);
            const auto table_offset
              = static_cast<size_t>(comp_desc.table.offset);
            if (!has_range(table_offset, records_total)) {
              std::cout << "          Renderables (" << comp_desc.table.count
                        << "): (not present in descriptor: offset="
                        << asset_dump_helpers::ToHexString(
                             comp_desc.table.offset)
                        << ", bytes=" << records_total << ", have "
                        << data->size() << ")\n";
              continue;
            }

            const uint32_t record_limit = ctx.verbose
              ? comp_desc.table.count
              : (std::min)(comp_desc.table.count, 16u);

            if (comp_desc.table.count == 0) {
              continue;
            }

            std::cout << "          Records:\n";
            for (uint32_t r = 0; r < record_limit; ++r) {
              RenderableRecord record {};
              const size_t record_off = table_offset + r * sizeof(record);
              if (!asset_dump_helpers::ReadStructAt(
                    *data, record_off, sizeof(record), &record)) {
                std::cout << "            Renderable[" << r
                          << "]: (insufficient data)\n";
                break;
              }

              const auto node_name = TryGetNodeName(record.node_index);

              std::cout << "            Renderable[" << r
                        << "]: node=" << record.node_index;
              if (!node_name.empty()) {
                std::cout << " (" << std::string(node_name) << ")";
              }
              std::cout << ", geometry="
                        << oxygen::data::to_string(record.geometry_key)
                        << ", visible=" << record.visible << "\n";
            }

            if (comp_desc.table.count > record_limit) {
              std::cout << "            ... ("
                        << (comp_desc.table.count - record_limit)
                        << " more components)\n";
            }
          }

          if (component_type == ComponentType::kPerspectiveCamera) {
            if (comp_desc.table.entry_size != sizeof(PerspectiveCameraRecord)) {
              std::cout
                << "          PerspectiveCameraRecord entry_size mismatch: "
                << "expected " << sizeof(PerspectiveCameraRecord) << ", got "
                << comp_desc.table.entry_size << "\n";
              continue;
            }

            const uint64_t records_total_u64
              = static_cast<uint64_t>(comp_desc.table.count)
              * static_cast<uint64_t>(comp_desc.table.entry_size);
            if (records_total_u64 > (std::numeric_limits<size_t>::max)()) {
              std::cout << "          Perspective cameras ("
                        << comp_desc.table.count
                        << "): (too large to inspect)\n";
              continue;
            }
            if (comp_desc.table.offset > (std::numeric_limits<size_t>::max)()) {
              std::cout << "          Perspective cameras ("
                        << comp_desc.table.count
                        << "): (offset too large to inspect)\n";
              continue;
            }

            const auto records_total = static_cast<size_t>(records_total_u64);
            const auto table_offset
              = static_cast<size_t>(comp_desc.table.offset);
            if (!has_range(table_offset, records_total)) {
              std::cout << "          Perspective cameras ("
                        << comp_desc.table.count
                        << "): (not present in descriptor: offset="
                        << asset_dump_helpers::ToHexString(
                             comp_desc.table.offset)
                        << ", bytes=" << records_total << ", have "
                        << data->size() << ")\n";
              continue;
            }

            const uint32_t record_limit = ctx.verbose
              ? comp_desc.table.count
              : (std::min)(comp_desc.table.count, 16u);

            if (comp_desc.table.count == 0) {
              continue;
            }

            std::cout << "          Records:\n";
            for (uint32_t r = 0; r < record_limit; ++r) {
              PerspectiveCameraRecord record {};
              const size_t record_off = table_offset + r * sizeof(record);
              if (!asset_dump_helpers::ReadStructAt(
                    *data, record_off, sizeof(record), &record)) {
                std::cout << "            PerspectiveCamera[" << r
                          << "]: (insufficient data)\n";
                break;
              }

              const auto node_name = TryGetNodeName(record.node_index);

              std::cout << "            PerspectiveCamera[" << r
                        << "]: node=" << record.node_index;
              if (!node_name.empty()) {
                std::cout << " (" << std::string(node_name) << ")";
              }
              std::cout << ", fov_y=" << record.fov_y
                        << ", aspect=" << record.aspect_ratio
                        << ", near=" << record.near_plane
                        << ", far=" << record.far_plane << "\n";
            }

            if (comp_desc.table.count > record_limit) {
              std::cout << "            ... ("
                        << (comp_desc.table.count - record_limit)
                        << " more components)\n";
            }
          }

          if (component_type == ComponentType::kOrthographicCamera) {
            if (comp_desc.table.entry_size
              != sizeof(OrthographicCameraRecord)) {
              std::cout
                << "          OrthographicCameraRecord entry_size mismatch: "
                << "expected " << sizeof(OrthographicCameraRecord) << ", got "
                << comp_desc.table.entry_size << "\n";
              continue;
            }

            const uint64_t records_total_u64
              = static_cast<uint64_t>(comp_desc.table.count)
              * static_cast<uint64_t>(comp_desc.table.entry_size);
            if (records_total_u64 > (std::numeric_limits<size_t>::max)()) {
              std::cout << "          Orthographic cameras ("
                        << comp_desc.table.count
                        << "): (too large to inspect)\n";
              continue;
            }
            if (comp_desc.table.offset > (std::numeric_limits<size_t>::max)()) {
              std::cout << "          Orthographic cameras ("
                        << comp_desc.table.count
                        << "): (offset too large to inspect)\n";
              continue;
            }

            const auto records_total = static_cast<size_t>(records_total_u64);
            const auto table_offset
              = static_cast<size_t>(comp_desc.table.offset);
            if (!has_range(table_offset, records_total)) {
              std::cout << "          Orthographic cameras ("
                        << comp_desc.table.count
                        << "): (not present in descriptor: offset="
                        << asset_dump_helpers::ToHexString(
                             comp_desc.table.offset)
                        << ", bytes=" << records_total << ", have "
                        << data->size() << ")\n";
              continue;
            }

            const uint32_t record_limit = ctx.verbose
              ? comp_desc.table.count
              : (std::min)(comp_desc.table.count, 16u);

            if (comp_desc.table.count == 0) {
              continue;
            }

            std::cout << "          Records:\n";
            for (uint32_t r = 0; r < record_limit; ++r) {
              OrthographicCameraRecord record {};
              const size_t record_off = table_offset + r * sizeof(record);
              if (!asset_dump_helpers::ReadStructAt(
                    *data, record_off, sizeof(record), &record)) {
                std::cout << "            OrthographicCamera[" << r
                          << "]: (insufficient data)\n";
                break;
              }

              const auto node_name = TryGetNodeName(record.node_index);

              std::cout << "            OrthographicCamera[" << r
                        << "]: node=" << record.node_index;
              if (!node_name.empty()) {
                std::cout << " (" << std::string(node_name) << ")";
              }
              std::cout << ", left=" << record.left
                        << ", right=" << record.right
                        << ", bottom=" << record.bottom
                        << ", top=" << record.top
                        << ", near=" << record.near_plane
                        << ", far=" << record.far_plane << "\n";
            }

            if (comp_desc.table.count > record_limit) {
              std::cout << "            ... ("
                        << (comp_desc.table.count - record_limit)
                        << " more components)\n";
            }
          }
        }

        std::cout << "\n";
      }
    }

    std::cout << "\n";
  }
};

} // namespace oxygen::content::pakdump
