//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneIds.h>

namespace oxygen::data::testing {

//! Builds a cooked scene descriptor in memory for tests.
/*!
 Nodes, component tables, script slots, start state and environment records
 are laid out in the cooked form a `SceneAsset` reads: descriptor, nodes,
 string table, component table directory, tables, script slots, then the
 environment block.
*/
class SceneAssetBuilder {
public:
  using NodeIndex = pak::world::SceneNodeIndexT;

  //! Adds a node; a node without a parent is a root.
  auto AddNode(std::string_view name, std::optional<NodeIndex> parent = {})
    -> NodeIndex
  {
    const auto index = static_cast<NodeIndex>(nodes_.size());
    auto node = pak::world::NodeRecord {};
    node.node_id = NodeId::FromStableIdentity(
      "/test/node/" + std::to_string(index) + "/" + std::string(name));
    node.scene_name_offset = strings_.Add(name);
    node.parent_index = parent.value_or(index);
    nodes_.push_back(node);
    return index;
  }

  //! Mutable access to a node record, to set flags or a transform.
  auto Node(const NodeIndex index) -> pak::world::NodeRecord&
  {
    return nodes_.at(index);
  }

  //! Adds a component table; records must be sorted by node index.
  template <typename Record>
  auto AddComponents(const ComponentType type, std::vector<Record> records)
    -> SceneAssetBuilder&
  {
    const auto bytes = std::as_bytes(std::span(records));
    tables_.push_back(Table {
      .type = type,
      .entry_size = sizeof(Record),
      .count = static_cast<uint32_t>(records.size()),
      .bytes = { bytes.begin(), bytes.end() },
    });
    return *this;
  }

  //! Adds one scripting component per node with its slots, in node order; a
  //! slot without an ID gets one from its node and position.
  auto AddScripting(const NodeIndex node,
    std::vector<pak::scripting::ScriptSlotRecord> slots) -> SceneAssetBuilder&
  {
    for (size_t i = 0; i < slots.size(); ++i) {
      if (slots.at(i).slot_id.IsNil()) {
        slots.at(i).slot_id = ScriptSlotId::FromStableIdentity(
          "/test/slot/" + std::to_string(node) + "/" + std::to_string(i));
      }
    }
    scripting_.push_back({
      .node_index = node,
      .slot_start_index = static_cast<uint32_t>(slots_.size()),
      .slot_count = static_cast<uint32_t>(slots.size()),
    });
    slots_.insert(slots_.end(), slots.begin(), slots.end());
    return *this;
  }

  auto SetFlags(const pak::world::SceneAssetFlags flags) -> SceneAssetBuilder&
  {
    flags_ = flags;
    return *this;
  }

  auto SetStartingCamera(const NodeIndex node) -> SceneAssetBuilder&
  {
    starting_camera_ = { .node_index = node };
    return *this;
  }

  //! Sets a starting camera that may name a node an instance brings in.
  auto SetStartingCamera(const pak::world::NodeRef& camera)
    -> SceneAssetBuilder&
  {
    starting_camera_ = camera;
    return *this;
  }

  //! Adds `text` to the descriptor's string table.
  auto AddString(const std::string_view text) -> pak::core::StringTableOffsetT
  {
    return strings_.Add(text);
  }

  //! Adds an environment system record; its header gives type and size.
  template <typename Record>
  auto AddEnvironment(const Record& record) -> SceneAssetBuilder&
  {
    Append(environment_, std::as_bytes(std::span(&record, 1)));
    ++environment_count_;
    return *this;
  }

  //! The cooked descriptor bytes.
  [[nodiscard]] auto Bytes() const -> std::vector<std::byte>
  {
    using pak::scripting::ScriptingComponentRecord;
    using pak::scripting::ScriptSlotRecord;
    using pak::world::NodeRecord;
    using pak::world::SceneAssetDesc;
    using pak::world::SceneComponentTableDesc;

    auto tables = tables_;
    if (!scripting_.empty()) {
      const auto bytes = std::as_bytes(std::span(scripting_));
      tables.push_back(Table {
        .type = ComponentType::kScripting,
        .entry_size = sizeof(ScriptingComponentRecord),
        .count = static_cast<uint32_t>(scripting_.size()),
        .bytes = { bytes.begin(), bytes.end() },
      });
    }

    auto desc = SceneAssetDesc {};
    desc.header.asset_type = static_cast<uint8_t>(AssetType::kScene);
    desc.header.version = pak::world::kSceneAssetVersion;
    desc.flags = flags_;
    desc.starting_camera = starting_camera_;
    auto offset = uint64_t { sizeof(SceneAssetDesc) };
    desc.nodes = {
      .offset = offset,
      .count = static_cast<uint32_t>(nodes_.size()),
      .entry_size = sizeof(NodeRecord),
    };
    offset += nodes_.size() * sizeof(NodeRecord);
    desc.header.strings = {
      .offset = static_cast<uint32_t>(offset),
      .size = strings_.Size(),
    };
    offset += strings_.Size();
    desc.component_table_directory_offset = offset;
    desc.component_table_count = static_cast<uint32_t>(tables.size());
    offset += tables.size() * sizeof(SceneComponentTableDesc);
    auto directory = std::vector<SceneComponentTableDesc> {};
    for (const auto& table : tables) {
      directory.push_back({
        .component_type = static_cast<uint32_t>(table.type),
        .table = {
          .offset = offset,
          .count = table.count,
          .entry_size = static_cast<uint32_t>(table.entry_size),
        },
      });
      offset += table.bytes.size();
    }
    if (!slots_.empty()) {
      desc.script_slots = {
        .offset = offset,
        .count = static_cast<uint32_t>(slots_.size()),
        .entry_size = sizeof(ScriptSlotRecord),
      };
    }

    auto bytes = std::vector<std::byte> {};
    Append(bytes, std::as_bytes(std::span(&desc, 1)));
    Append(bytes, std::as_bytes(std::span(nodes_)));
    Append(bytes, strings_.Bytes());
    Append(bytes, std::as_bytes(std::span(directory)));
    for (const auto& table : tables) {
      Append(bytes, table.bytes);
    }
    Append(bytes, std::as_bytes(std::span(slots_)));
    if (environment_count_ != 0U) {
      const auto header = pak::world::SceneEnvironmentBlockHeader {
        .byte_size
        = static_cast<uint32_t>(sizeof(pak::world::SceneEnvironmentBlockHeader)
          + environment_.size()),
        .systems_count = environment_count_,
      };
      Append(bytes, std::as_bytes(std::span(&header, 1)));
      Append(bytes, environment_);
    }
    return bytes;
  }

  //! A scene asset parsed from the cooked bytes.
  [[nodiscard]] auto Build(
    const AssetKey& key = AssetKey::FromVirtualPath("/test/Scene.oscene")) const
    -> std::shared_ptr<SceneAsset>
  {
    return std::make_shared<SceneAsset>(key, Bytes());
  }

private:
  struct Table {
    ComponentType type {};
    size_t entry_size { 0 };
    uint32_t count { 0 };
    std::vector<std::byte> bytes;
  };

  static auto Append(
    std::vector<std::byte>& out, const std::span<const std::byte> bytes) -> void
  {
    out.insert(out.end(), bytes.begin(), bytes.end());
  }

  std::vector<pak::world::NodeRecord> nodes_;
  DescriptorStringsBuilder strings_;
  std::vector<Table> tables_;
  std::vector<pak::scripting::ScriptingComponentRecord> scripting_;
  std::vector<pak::scripting::ScriptSlotRecord> slots_;
  pak::world::SceneAssetFlags flags_ { pak::world::SceneAssetFlags::kNone };
  pak::world::NodeRef starting_camera_ {
    .node_index = pak::world::kNoStartingCamera,
  };
  std::vector<std::byte> environment_;
  uint32_t environment_count_ { 0 };
};

} // namespace oxygen::data::testing
