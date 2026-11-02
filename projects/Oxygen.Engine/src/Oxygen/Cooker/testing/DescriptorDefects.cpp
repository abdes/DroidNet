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
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <Oxygen/Cooker/testing/DescriptorDefects.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/DescriptorHash.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/StableId.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Serio/ByteReader.h>

namespace oxygen::cooker::testing {

namespace {

  namespace core = data::pak::core;
  namespace world = data::pak::world;
  namespace script = data::pak::scripting;
  namespace physics = data::pak::physics;

  //! The record `T` at `offset` of `bytes`.
  template <class T>
  auto ReadRecord(const std::span<const std::byte> bytes, const size_t offset)
    -> T
  {
    const auto read = serio::CopyAt<T>(bytes, serio::ByteOffset { offset });
    if (!read) {
      throw std::invalid_argument("the descriptor is too small for its record");
    }
    return *read;
  }

  //! Writes `record` at `offset` of the descriptor `bytes`, and records
  //! its new content hash, so the edit breaks no other rule.
  template <class T>
  auto WriteRecord(
    std::vector<std::byte>& bytes, const size_t offset, const T& record) -> void
  {
    if (offset + sizeof(T) > bytes.size()) {
      throw std::invalid_argument("the descriptor is too small for its record");
    }
    std::ranges::copy(std::bit_cast<std::array<std::byte, sizeof(T)>>(record),
      std::span { bytes }.subspan(offset).begin());
    data::StampDescriptorHash(bytes);
  }

  //! Reads the record `T` at `offset` of `bytes`, lets `edit` change it and
  //! writes it back.
  template <class T, class Edit>
  auto EditRecord(std::vector<std::byte>& bytes, const size_t offset,
    const Edit& edit) -> void
  {
    auto record = ReadRecord<T>(bytes, offset);
    edit(record);
    WriteRecord(bytes, offset, record);
  }

  //! The bytes of the descriptor `bytes` that hold its content hash.
  auto HashField(std::vector<std::byte>& bytes) -> std::span<std::byte>
  {
    constexpr auto kOffset = offsetof(core::AssetHeader, content_hash);
    constexpr auto kSize = sizeof(core::AssetHeader::content_hash);
    if (bytes.size() < sizeof(core::AssetHeader)) {
      throw std::invalid_argument("the descriptor is smaller than its header");
    }
    return std::span { bytes }.subspan(kOffset, kSize);
  }

  //! The offset of the element `index` of `table`, of `size` bytes.
  auto ElementOffset(const world::SceneDataTable& table, const uint32_t index,
    const size_t size, const char* what) -> size_t
  {
    if (index >= table.count || table.entry_size != size) {
      throw std::invalid_argument(
        std::string("the descriptor has no such ") + what);
    }
    return static_cast<size_t>(table.offset)
      + (static_cast<size_t>(index) * table.entry_size);
  }

  //! Where the scene's environment block starts: after the last of its
  //! tables, slots and parameters.
  auto EnvironmentOffset(const std::span<const std::byte> scene) -> size_t
  {
    const auto desc = ReadRecord<world::SceneAssetDesc>(scene, 0U);
    const auto end_of = [](const world::SceneDataTable& table) -> size_t {
      return static_cast<size_t>(table.offset)
        + (static_cast<size_t>(table.count) * table.entry_size);
    };
    auto end = std::max<size_t>(sizeof(desc), end_of(desc.nodes));
    end = std::max<size_t>(
      end, size_t { desc.header.strings.offset } + desc.header.strings.size);
    end = std::max<size_t>(end,
      static_cast<size_t>(desc.component_table_directory_offset)
        + (size_t { desc.component_table_count }
          * sizeof(world::SceneComponentTableDesc)));
    for (auto i = uint32_t { 0U }; i < desc.component_table_count; ++i) {
      const auto entry = ReadRecord<world::SceneComponentTableDesc>(scene,
        static_cast<size_t>(desc.component_table_directory_offset)
          + (size_t { i } * sizeof(world::SceneComponentTableDesc)));
      end = std::max(end, end_of(entry.table));
    }
    end = std::max(end, end_of(desc.script_slots));
    for (auto i = uint32_t { 0U }; i < desc.script_slots.count; ++i) {
      const auto slot = ReadRecord<script::ScriptSlotRecord>(scene,
        ElementOffset(
          desc.script_slots, i, sizeof(script::ScriptSlotRecord), "slot"));
      end = std::max<size_t>(end,
        static_cast<size_t>(slot.params_array_offset)
          + (size_t { slot.params_count } * sizeof(script::ScriptParamRecord)));
    }
    return end;
  }

} // namespace

auto DescriptorDefects::SetAssetType(
  std::vector<std::byte>& descriptor, const data::AssetType type) -> void
{
  EditRecord<core::AssetHeader>(descriptor, 0U, [type](auto& header) -> void {
    header.asset_type = static_cast<uint8_t>(type);
  });
}

auto DescriptorDefects::ClearContentHash(std::vector<std::byte>& descriptor)
  -> void
{
  std::ranges::fill(HashField(descriptor), std::byte { 0 });
}

auto DescriptorDefects::CorruptContentHash(std::vector<std::byte>& descriptor)
  -> void
{
  HashField(descriptor).front() ^= std::byte { 0xFF };
}

auto DescriptorDefects::SetVersion(
  std::vector<std::byte>& descriptor, const uint8_t version) -> void
{
  EditRecord<core::AssetHeader>(descriptor, 0U,
    [version](auto& header) -> void { header.version = version; });
}

auto DescriptorDefects::ClearSurface(
  std::vector<std::byte>& geometry, const data::SurfaceId& surface) -> void
{
  // A surface identity is a 16-byte value every record that names the
  // surface holds as is, so it is cleared wherever it occurs, whatever the
  // layout around it.
  const auto identity = surface.Bytes();
  const auto pattern = std::as_bytes(std::span(identity));
  auto cleared = false;
  auto at = geometry.begin();
  while ((at = std::search(at, geometry.end(), pattern.begin(), pattern.end()))
    != geometry.end()) {
    at = std::fill_n(at, pattern.size(), std::byte { 0 });
    cleared = true;
  }
  if (!cleared) {
    throw std::invalid_argument(
      "the geometry names no surface " + data::to_string(surface));
  }
  data::StampDescriptorHash(geometry);
}

auto DescriptorDefects::PointNodeTablePastTheEnd(std::vector<std::byte>& scene)
  -> void
{
  const auto size = scene.size();
  EditRecord<world::SceneAssetDesc>(scene, 0U, [size](auto& desc) -> void {
    desc.nodes.count
      = static_cast<uint32_t>(size / sizeof(world::NodeRecord)) + 1U;
  });
}

auto DescriptorDefects::SetSceneFlags(
  std::vector<std::byte>& scene, const uint32_t flags) -> void
{
  EditRecord<world::SceneAssetDesc>(scene, 0U, [flags](auto& desc) -> void {
    desc.flags = static_cast<world::SceneAssetFlags>(flags);
  });
}

auto DescriptorDefects::SetStartingCamera(
  std::vector<std::byte>& scene, const world::NodeRef camera) -> void
{
  EditRecord<world::SceneAssetDesc>(
    scene, 0U, [camera](auto& desc) -> void { desc.starting_camera = camera; });
}

auto DescriptorDefects::NodeAt(const std::span<const std::byte> scene,
  const uint32_t index) -> world::NodeRecord
{
  const auto desc = ReadRecord<world::SceneAssetDesc>(scene, 0U);
  return ReadRecord<world::NodeRecord>(
    scene, ElementOffset(desc.nodes, index, sizeof(world::NodeRecord), "node"));
}

auto DescriptorDefects::SetNode(std::vector<std::byte>& scene,
  const uint32_t index, const world::NodeRecord& node) -> void
{
  const auto desc = ReadRecord<world::SceneAssetDesc>(scene, 0U);
  WriteRecord(scene,
    ElementOffset(desc.nodes, index, sizeof(world::NodeRecord), "node"), node);
}

auto DescriptorDefects::SetComponentTableType(std::vector<std::byte>& scene,
  const data::ComponentType table, const uint32_t type) -> void
{
  EditRecord<world::SceneComponentTableDesc>(scene,
    TableEntryOffset(scene, table),
    [type](auto& entry) -> void { entry.component_type = type; });
}

auto DescriptorDefects::SetComponentEntrySize(std::vector<std::byte>& scene,
  const data::ComponentType type, const uint32_t size) -> void
{
  EditRecord<world::SceneComponentTableDesc>(scene,
    TableEntryOffset(scene, type),
    [size](auto& entry) -> void { entry.table.entry_size = size; });
}

auto DescriptorDefects::ScriptSlotAt(const std::span<const std::byte> scene,
  const uint32_t index) -> script::ScriptSlotRecord
{
  const auto desc = ReadRecord<world::SceneAssetDesc>(scene, 0U);
  return ReadRecord<script::ScriptSlotRecord>(scene,
    ElementOffset(
      desc.script_slots, index, sizeof(script::ScriptSlotRecord), "slot"));
}

auto DescriptorDefects::SetScriptSlot(std::vector<std::byte>& scene,
  const uint32_t index, const script::ScriptSlotRecord& slot) -> void
{
  const auto desc = ReadRecord<world::SceneAssetDesc>(scene, 0U);
  WriteRecord(scene,
    ElementOffset(
      desc.script_slots, index, sizeof(script::ScriptSlotRecord), "slot"),
    slot);
}

auto DescriptorDefects::EnvironmentRecord(std::vector<std::byte>& scene,
  const world::EnvironmentComponentType type) -> std::span<std::byte>
{
  const auto block = EnvironmentOffset(scene);
  const auto header
    = ReadRecord<world::SceneEnvironmentBlockHeader>(scene, block);
  auto cursor = block + sizeof(header);
  for (auto i = uint32_t { 0U }; i < header.systems_count; ++i) {
    const auto record
      = ReadRecord<world::SceneEnvironmentSystemRecordHeader>(scene, cursor);
    if (record.system_type == static_cast<uint32_t>(type)) {
      if (cursor + record.record_size > scene.size()) {
        throw std::invalid_argument("the environment record overruns");
      }
      return std::span(scene).subspan(cursor, record.record_size);
    }
    cursor += record.record_size;
  }
  throw std::invalid_argument("the scene has no such environment record");
}

auto DescriptorDefects::SetEnvironmentRecordSize(std::vector<std::byte>& scene,
  const world::EnvironmentComponentType type, const uint32_t size) -> void
{
  const auto record = EnvironmentRecord(scene, type);
  const auto at = static_cast<size_t>(record.data() - scene.data());
  EditRecord<world::SceneEnvironmentSystemRecordHeader>(
    scene, at, [size](auto& header) -> void { header.record_size = size; });
}

auto DescriptorDefects::SetEnvironmentRecordType(std::vector<std::byte>& scene,
  const world::EnvironmentComponentType record, const uint32_t type) -> void
{
  const auto bytes = EnvironmentRecord(scene, record);
  const auto at = static_cast<size_t>(bytes.data() - scene.data());
  EditRecord<world::SceneEnvironmentSystemRecordHeader>(
    scene, at, [type](auto& header) -> void { header.system_type = type; });
}

auto DescriptorDefects::SetEnvironmentRecordCount(
  std::vector<std::byte>& scene, const uint32_t count) -> void
{
  EditRecord<world::SceneEnvironmentBlockHeader>(scene,
    EnvironmentOffset(scene),
    [count](auto& header) -> void { header.systems_count = count; });
}

auto DescriptorDefects::DropEnvironmentBlock(std::vector<std::byte>& scene)
  -> void
{
  scene.resize(EnvironmentOffset(scene));
  data::StampDescriptorHash(scene);
}

auto DescriptorDefects::SetSidecarScene(
  std::vector<std::byte>& sidecar, const data::AssetKey& scene) -> void
{
  EditRecord<physics::PhysicsSceneAssetDesc>(sidecar, 0U,
    [&scene](auto& desc) -> void { desc.target_scene_key = scene; });
}

auto DescriptorDefects::SetSidecarNodeCount(
  std::vector<std::byte>& sidecar, const uint32_t count) -> void
{
  EditRecord<physics::PhysicsSceneAssetDesc>(sidecar, 0U,
    [count](auto& desc) -> void { desc.target_node_count = count; });
}

auto DescriptorDefects::SetSidecarBackend(std::vector<std::byte>& sidecar,
  const physics::PhysicsBackend backend) -> void
{
  EditRecord<physics::PhysicsSceneAssetDesc>(
    sidecar, 0U, [backend](auto& desc) -> void { desc.backend = backend; });
}

auto DescriptorDefects::CorruptSidecarSceneHash(std::vector<std::byte>& sidecar)
  -> void
{
  EditRecord<physics::PhysicsSceneAssetDesc>(sidecar, 0U,
    [](auto& desc) -> void { desc.target_scene_content_hash[0] ^= 0xFFU; });
}

auto DescriptorDefects::SetBindingEntrySize(std::vector<std::byte>& sidecar,
  const physics::PhysicsBindingType type, const uint32_t size) -> void
{
  const auto desc = ReadRecord<physics::PhysicsSceneAssetDesc>(sidecar, 0U);
  for (auto i = uint32_t { 0U }; i < desc.component_table_count; ++i) {
    const auto at = static_cast<size_t>(desc.component_table_directory_offset)
      + (size_t { i } * sizeof(physics::PhysicsComponentTableDesc));
    if (ReadRecord<physics::PhysicsComponentTableDesc>(sidecar, at).binding_type
      == type) {
      EditRecord<physics::PhysicsComponentTableDesc>(sidecar, at,
        [size](auto& entry) -> void { entry.table.entry_size = size; });
      return;
    }
  }
  throw std::invalid_argument("the sidecar has no such binding table");
}

auto DescriptorDefects::RecordOffset(const std::span<const std::byte> scene,
  const data::ComponentType type, const uint32_t index, const size_t size)
  -> size_t
{
  const auto entry = ReadRecord<world::SceneComponentTableDesc>(
    scene, TableEntryOffset(scene, type));
  return ElementOffset(entry.table, index, size, "component");
}

auto DescriptorDefects::TableEntryOffset(const std::span<const std::byte> scene,
  const data::ComponentType type) -> size_t
{
  const auto desc = ReadRecord<world::SceneAssetDesc>(scene, 0U);
  for (auto i = uint32_t { 0U }; i < desc.component_table_count; ++i) {
    const auto at = static_cast<size_t>(desc.component_table_directory_offset)
      + (size_t { i } * sizeof(world::SceneComponentTableDesc));
    if (ReadRecord<world::SceneComponentTableDesc>(scene, at).component_type
      == static_cast<uint32_t>(type)) {
      return at;
    }
  }
  throw std::invalid_argument("the scene has no such component table");
}

} // namespace oxygen::cooker::testing
