//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/test/Support/TestValues.h>
#include <Oxygen/Cooker/testing/Descriptors.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormatSerioWriters.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

//! Scene roots shared by the plan-builder, patch and repack tests.
namespace oxygen::content::pak::test {

using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::SeedBytes;

//=== Descriptor bytes ===----------------------------------------------------//

//! `bytes` with the SHA-256 of the descriptor (its hash field zeroed) stored
//! in its header, as the importer stores it.
[[nodiscard]] inline auto WithDescriptorHash(std::vector<std::byte> bytes)
  -> std::vector<std::byte>
{
  const auto digest = oxygen::base::ComputeSha256(bytes);
  auto field = std::span(bytes).subspan(
    offsetof(data::pak::core::AssetHeader, content_hash), digest.size());
  std::memcpy(field.data(), digest.data(), digest.size());
  return bytes;
}

//! The physics sidecar of the scene `scene_key` whose descriptor is
//! `scene_bytes`.
[[nodiscard]] inline auto PhysicsSidecarDescriptor(
  const data::AssetKey& scene_key, const std::span<const std::byte> scene_bytes)
  -> std::vector<std::byte>
{
  return WithDescriptorHash(
    cooker::testing::PhysicsSidecarDescriptor(scene_key, scene_bytes).bytes);
}

//=== Scenes that bind scripts ===--------------------------------------------//

//! The key of script `index` of the scene of `seed`.
[[nodiscard]] inline auto ScriptKey(const uint8_t seed, const uint8_t index)
  -> data::AssetKey
{
  return cooker::test::MakeAssetKey(static_cast<uint8_t>((seed * 10U) + index));
}

//! The parameter value of script `index` of the scene of `seed`.
[[nodiscard]] constexpr auto ScriptParameter(
  const uint8_t seed, const uint32_t index) -> int32_t
{
  return (seed * 100) + static_cast<int32_t>(index);
}

//! A scene of two nodes, each with one scripting slot that binds
//! `ScriptKey(seed, i)` with the int parameter `ScriptParameter(seed, i)`.
[[nodiscard]] inline auto BuildSceneDescriptorWithScriptingSlots(
  const uint8_t seed) -> std::vector<std::byte>
{
  auto desc = data::pak::world::SceneAssetDesc {};
  desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kScene);
  desc.header.version = data::pak::world::kSceneAssetVersion;
  std::array<data::pak::world::NodeRecord, 2> nodes {};
  nodes.front().node_id
    = data::NodeId::FromStableIdentity("/test/node/" + std::to_string(seed));
  nodes.back().node_id = data::NodeId::FromStableIdentity(
    "/test/node/" + std::to_string(seed + 2U));
  nodes.front().parent_index = 0U;
  nodes.back().parent_index = 0U;
  desc.nodes = {
    .offset = sizeof(desc),
    .count = 2U,
    .entry_size = sizeof(data::pak::world::NodeRecord),
  };
  auto strings = data::DescriptorStringsBuilder {};
  const auto key_offset = strings.Add("v");
  desc.header.strings = {
    .offset = sizeof(desc) + sizeof(nodes),
    .size = strings.Size(),
  };
  desc.component_table_directory_offset
    = desc.header.strings.offset + desc.header.strings.size;
  desc.component_table_count = 1U;
  auto table = data::pak::world::SceneComponentTableDesc {};
  table.component_type = static_cast<uint32_t>(data::ComponentType::kScripting);
  table.table = {
    .offset = desc.component_table_directory_offset + sizeof(table),
    .count = 2U,
    .entry_size = sizeof(data::pak::scripting::ScriptingComponentRecord),
  };
  std::array<data::pak::scripting::ScriptingComponentRecord, 2> components {};
  std::array<data::pak::scripting::ScriptSlotRecord, 2> slots {};
  std::array<data::pak::scripting::ScriptParamRecord, 2> parameters {};
  desc.script_slots = {
    .offset = table.table.offset + sizeof(components),
    .count = 2U,
    .entry_size = sizeof(data::pak::scripting::ScriptSlotRecord),
  };
  for (uint8_t i = 0; i < slots.size(); ++i) {
    auto& component = components.at(i);
    component.node_index = i;
    component.slot_start_index = i;
    component.slot_count = 1U;
    auto& slot = slots.at(i);
    slot.slot_id = data::ScriptSlotId::FromStableIdentity(
      "/test/slot/" + std::to_string(i));
    slot.script_asset_key = ScriptKey(seed, i);
    slot.params_array_offset = desc.script_slots.offset + sizeof(slots)
      + (i * sizeof(data::pak::scripting::ScriptParamRecord));
    slot.params_count = 1U;
    auto& parameter = parameters.at(i);
    parameter.key_offset = key_offset;
    parameter.type = data::pak::scripting::ScriptParamType::kInt32;
    parameter.value.as_int32 = ScriptParameter(seed, i);
  }
  data::pak::world::SceneEnvironmentBlockHeader environment {};
  environment.byte_size = sizeof(environment);
  std::vector<std::byte> bytes;
  const auto append = [&bytes](const auto& record) -> auto {
    const auto packed = std::as_bytes(std::span(&record, 1U));
    bytes.insert(bytes.end(), packed.begin(), packed.end());
  };
  append(desc);
  append(nodes);
  const auto table_bytes = strings.Bytes();
  bytes.insert(bytes.end(), table_bytes.begin(), table_bytes.end());
  append(table);
  append(components);
  append(slots);
  append(parameters);
  append(environment);
  return bytes;
}

//! A loose root holding the scene of `seed`, keyed `MakeAssetKey(seed)`, and
//! the two scripts its slots bind.
struct SceneScripts final {
  LooseCookedRoot root;
  data::AssetKey scene;
};

[[nodiscard]] inline auto MakeSceneScripts(const uint8_t seed) -> SceneScripts
{
  auto root = LooseCookedRoot { "Game", seed };
  auto keys = std::vector<data::KeyReference> {};
  for (uint8_t i = 0U; i < 2U; ++i) {
    auto script = data::pak::scripting::ScriptAssetDesc {};
    script.header.asset_type = static_cast<uint8_t>(data::AssetType::kScript);
    script.header.version = data::pak::scripting::kScriptAssetVersion;
    keys.push_back({
      .key = root.Add({
        .type = data::AssetType::kScript,
        .leaf
        = "Logic" + std::to_string(seed) + "-" + std::to_string(i) + ".oscript",
        .descriptor = data::MakeDescriptorBytes(script),
        .key = ScriptKey(seed, i),
      }),
      .kind = data::KeyReferenceKind::kAsset,
      .expected_type = data::AssetType::kScript,
    });
  }
  const auto scene = root.Add({
    .type = data::AssetType::kScene,
    .leaf = "Scene" + std::to_string(seed) + ".oscene",
    .descriptor = BuildSceneDescriptorWithScriptingSlots(seed),
    .references = data::AssetReferences::Create({}, std::move(keys)).value(),
    .key = cooker::test::MakeAssetKey(seed),
  });
  return { .root = std::move(root), .scene = scene };
}

//=== Scenes that bind textures through a mask ===----------------------------//

//! A loose root holding a scene whose post-process metering mask binds the
//! root's one texture, a material whose base color binds it too, and the
//! scene's physics sidecar. Merging two of them has to remap the second
//! root's texture.
struct SceneMask final {
  LooseCookedRoot root;
  data::AssetKey scene;
  data::AssetKey material;
  data::AssetKey sidecar;
  //! The texture's payload: `SeedBytes(seed)`.
  std::vector<std::byte> texture_payload;
};

[[nodiscard]] inline auto MakeSceneMask(const uint8_t seed) -> SceneMask
{
  namespace world = data::pak::world;
  namespace serio = oxygen::serio;
  constexpr auto require = [](const auto& result) -> void {
    if (!result) {
      throw std::runtime_error("scene mask descriptor could not be encoded");
    }
  };

  auto out = SceneMask {
    .root = LooseCookedRoot { "Game", seed },
    .scene = {},
    .material = {},
    .sidecar = {},
    .texture_payload = SeedBytes(seed),
  };
  const auto texture = out.root.AddTexture(out.texture_payload);
  const auto binds_texture = data::AssetReferences::Create(
    { { .kind = data::ResourceKind::kTexture, .index = texture } }, {})
                               .value();

  auto post = world::PostProcessVolumeEnvironmentRecord {};
  post.auto_exposure_metering_mask = data::ResourceReferenceIndex { 0U };
  auto descriptor = world::SceneAssetDesc {};
  descriptor.header.asset_type = static_cast<uint8_t>(data::AssetType::kScene);
  descriptor.header.version = world::kSceneAssetVersion;
  const auto strings = data::DescriptorStringsBuilder {};
  descriptor.header.strings = {
    .offset = sizeof(descriptor),
    .size = strings.Size(),
  };
  const auto environment = world::SceneEnvironmentBlockHeader {
    .byte_size = sizeof(world::SceneEnvironmentBlockHeader) + sizeof(post),
    .systems_count = 1U,
  };
  auto scene_stream = serio::MemoryStream {};
  auto scene_writer = serio::Writer(scene_stream);
  const auto packed = scene_writer.ScopedAlignment(1);
  require(scene_writer.Write(descriptor));
  require(scene_writer.WriteBlob(strings.Bytes()));
  require(scene_writer.Write(environment));
  require(serio::Store(scene_writer, post));
  const auto scene_bytes = WithDescriptorHash(
    { scene_stream.Data().begin(), scene_stream.Data().end() });
  out.scene = out.root.Add({
    .type = data::AssetType::kScene,
    .leaf = "Scene" + std::to_string(seed) + ".oscene",
    .descriptor = scene_bytes,
    .references = binds_texture,
  });

  auto material = data::pak::render::MaterialAssetDesc {};
  material.header.asset_type = static_cast<uint8_t>(data::AssetType::kMaterial);
  material.header.version = data::pak::render::kMaterialAssetVersion;
  material.base_color_texture = data::ResourceReferenceIndex { 0U };
  auto material_bytes = std::vector<std::byte> {};
  const auto record = std::as_bytes(std::span { &material, 1U });
  material_bytes.assign(record.begin(), record.end());
  data::AppendDescriptorStrings(
    material_bytes, data::DescriptorStringsBuilder {});
  out.material = out.root.Add({
    .type = data::AssetType::kMaterial,
    .leaf = "Material" + std::to_string(seed) + ".omat",
    .descriptor = WithDescriptorHash(std::move(material_bytes)),
    .references = binds_texture,
  });

  out.sidecar = out.root.Add({
    .type = data::AssetType::kPhysicsScene,
    .leaf = "Physics" + std::to_string(seed) + ".opscene",
    .descriptor = PhysicsSidecarDescriptor(out.scene, scene_bytes),
  });
  return out;
}

} // namespace oxygen::content::pak::test
