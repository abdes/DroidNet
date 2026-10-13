//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "PakTestSupport.h"

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Pak/PakPlanBuilder.h>
#include <Oxygen/Cooker/Pak/PakWriter.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/PakFormatSerioWriters.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

//! Scene fixtures shared by the plan-builder, patch and repack tests.
namespace oxygen::content::pak::test {

//=== Scenes that bind scripts ===--------------------------------------------//

[[nodiscard]] inline auto BuildSceneDescriptorWithScriptingSlots(
  const uint8_t seed) -> std::vector<std::byte>
{
  auto desc = data::pak::world::SceneAssetDesc {};
  desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kScene);
  desc.header.version = data::pak::world::kSceneAssetVersion;
  std::array<data::pak::world::NodeRecord, 2> nodes {};
  nodes.front().node_id = MakeAssetKey(seed);
  nodes.back().node_id = MakeAssetKey(static_cast<uint8_t>(seed + 2U));
  nodes.front().parent_index = 0U;
  nodes.back().parent_index = 0U;
  desc.nodes = {
    .offset = sizeof(desc),
    .count = 2U,
    .entry_size = sizeof(data::pak::world::NodeRecord),
  };
  desc.scene_strings.offset = sizeof(desc) + sizeof(nodes);
  desc.scene_strings.size = 1U;
  desc.component_table_directory_offset = desc.scene_strings.offset + 1U;
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
  for (size_t i = 0; i < slots.size(); ++i) {
    auto& component = components.at(i);
    component.node_index = static_cast<uint32_t>(i);
    component.slot_start_index = static_cast<uint32_t>(i);
    component.slot_count = 1U;
    auto& slot = slots.at(i);
    slot.script_asset_key
      = MakeAssetKey(static_cast<uint8_t>((seed * 10U) + i));
    slot.params_array_offset = desc.script_slots.offset + sizeof(slots)
      + (i * sizeof(data::pak::scripting::ScriptParamRecord));
    slot.params_count = 1U;
    auto& parameter = parameters.at(i);
    parameter.key[0] = 'v';
    parameter.type = data::pak::scripting::ScriptParamType::kInt32;
    parameter.value.as_int32 = (seed * 100) + static_cast<int32_t>(i);
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
  bytes.push_back(std::byte { 0 });
  append(table);
  append(components);
  append(slots);
  append(parameters);
  append(environment);
  return bytes;
}

[[nodiscard]] inline auto MakeSceneScriptAssets(const uint8_t seed)
  -> std::vector<AssetSpec>
{
  const auto scene_bytes = BuildSceneDescriptorWithScriptingSlots(seed);
  std::vector<AssetSpec> assets;
  std::vector<data::KeyReference> keys;
  for (uint8_t i = 0U; i < 2U; ++i) {
    const auto key = MakeAssetKey(static_cast<uint8_t>((seed * 10U) + i));
    keys.push_back({
      .key = key,
      .kind = data::KeyReferenceKind::kAsset,
      .expected_type = data::AssetType::kScript,
    });
    data::pak::scripting::ScriptAssetDesc script_desc {};
    script_desc.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kScript);
    script_desc.header.version = data::pak::scripting::kScriptAssetVersion;
    const auto bytes = std::as_bytes(std::span(&script_desc, 1U));
    const auto name
      = "Logic" + std::to_string(seed) + "-" + std::to_string(i) + ".oscript";
    assets.push_back({
      .key = key,
      .asset_type = data::AssetType::kScript,
      .descriptor_relpath = name,
      .virtual_path = "/Game/" + name,
      .descriptor_size = bytes.size(),
      .descriptor_sha = {},
      .descriptor_payload = { bytes.begin(), bytes.end() },
      .references = {},
    });
  }
  assets.push_back({
    .key = MakeAssetKey(seed),
    .asset_type = data::AssetType::kScene,
    .descriptor_relpath = "Scene.oscene",
    .virtual_path = "/Game/Scene" + std::to_string(seed) + ".oscene",
    .descriptor_size = scene_bytes.size(),
    .descriptor_sha = {},
    .descriptor_payload = scene_bytes,
    .references = data::AssetReferences::Create({}, std::move(keys)).value(),
  });
  return assets;
}

//=== Scenes that bind textures through a mask ===----------------------------//

//! Two loose sources, each with a scene, a material and a physics sidecar.
/*!
 Every asset binds texture index 1 of its own source's texture table, so
 merging the sources has to remap the binding of the second one.
*/
struct SceneMaskSources final {
  std::vector<data::CookedSource> sources;
  size_t scene_descriptor_size = 0U;
};

//! Writes the sources under `root`; throws `std::runtime_error` on failure.
[[nodiscard]] inline auto WriteSceneMaskSources(
  const std::filesystem::path& root) -> SceneMaskSources
{
  namespace world = data::pak::world;
  namespace serio = oxygen::serio;
  constexpr auto require = [](const auto& result) -> void {
    if (!result) {
      throw std::runtime_error("scene mask fixture could not be written");
    }
  };

  auto post = world::PostProcessVolumeEnvironmentRecord {};
  post.auto_exposure_metering_mask = data::ResourceReferenceIndex { 0U };
  auto descriptor = world::SceneAssetDesc {};
  descriptor.header.asset_type = static_cast<uint8_t>(data::AssetType::kScene);
  descriptor.header.version = world::kSceneAssetVersion;
  const auto environment = world::SceneEnvironmentBlockHeader {
    .byte_size = sizeof(world::SceneEnvironmentBlockHeader) + sizeof(post),
    .systems_count = 1U,
  };
  serio::MemoryStream scene_stream;
  serio::Writer scene_writer(scene_stream);
  const auto scene_packed = scene_writer.ScopedAlignment(1);
  require(scene_writer.Write(descriptor));
  require(scene_writer.Write(environment));
  require(serio::Store(scene_writer, post));
  auto scene_bytes = std::vector<std::byte>(
    scene_stream.Data().begin(), scene_stream.Data().end());
  EnableDescriptorHash(scene_bytes);

  serio::MemoryStream texture_stream;
  serio::Writer texture_writer(texture_stream);
  const auto texture_packed = texture_writer.ScopedAlignment(1);
  require(texture_writer.Write(data::pak::core::TextureResourceDesc {}));
  require(texture_writer.Write(data::pak::core::TextureResourceDesc {
    .data_offset = 0U,
    .size_bytes = 4U,
    .texture_type = static_cast<uint8_t>(oxygen::TextureType::kTexture2D),
    .compression_type = 0U,
    .width = 1U,
    .height = 1U,
    .depth = 1U,
    .array_layers = 1U,
    .mip_levels = 1U,
    .format = static_cast<uint8_t>(oxygen::Format::kRGBA8UNorm),
    .alignment = 256U,
  }));
  const auto texture_table = texture_stream.Data();

  auto result
    = SceneMaskSources { .scene_descriptor_size = scene_bytes.size() };
  for (const auto seed : { uint8_t { 1U }, uint8_t { 2U } }) {
    const auto source_root = root / std::to_string(seed);
    const auto asset = AssetSpec {
      .key = MakeAssetKey(static_cast<uint8_t>(seed * 10U)),
      .asset_type = data::AssetType::kScene,
      .descriptor_relpath = "Scene.oscene",
      .virtual_path = "/Game/Scene" + std::to_string(seed) + ".oscene",
      .descriptor_size = scene_bytes.size(),
      .descriptor_sha = oxygen::base::ComputeSha256(scene_bytes),
      .descriptor_payload = { scene_bytes.begin(), scene_bytes.end() },
      .references = data::AssetReferences::Create(
        {
          {
            .kind = data::ResourceKind::kTexture,
            .index = oxygen::ResourceIndexT { 1U },
          },
        },
        {})
        .value(),
    };
    const auto files = std::array {
      FileSpec {
        .kind = lc::FileKind::kTexturesTable,
        .relpath = "textures.table",
        .payload = { texture_table.begin(), texture_table.end() },
      },
      FileSpec {
        .kind = lc::FileKind::kTexturesData,
        .relpath = "textures.data",
        .payload = std::vector<std::byte>(4U, static_cast<std::byte>(seed)),
      },
    };
    auto material = data::pak::render::MaterialAssetDesc {};
    material.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kMaterial);
    material.header.version = data::pak::render::kMaterialAssetVersion;
    material.base_color_texture = data::ResourceReferenceIndex { 0U };
    serio::MemoryStream material_stream;
    serio::Writer material_writer(material_stream);
    const auto material_packed = material_writer.ScopedAlignment(1);
    require(
      material_writer.WriteBlob(std::as_bytes(std::span { &material, 1U })));
    auto material_bytes = std::vector<std::byte>(
      material_stream.Data().begin(), material_stream.Data().end());
    EnableDescriptorHash(material_bytes);
    const auto material_asset = AssetSpec {
      .key = MakeAssetKey(static_cast<uint8_t>(seed + 10U)),
      .asset_type = data::AssetType::kMaterial,
      .descriptor_relpath = "Material.omat",
      .virtual_path = "/Game/Material" + std::to_string(seed) + ".omat",
      .descriptor_size = material_bytes.size(),
      .descriptor_sha = oxygen::base::ComputeSha256(material_bytes),
      .descriptor_payload = { material_bytes.begin(), material_bytes.end() },
      .references = data::AssetReferences::Create(
        {
          {
            .kind = data::ResourceKind::kTexture,
            .index = oxygen::ResourceIndexT { 1U },
          },
        },
        {})
        .value(),
    };
    const auto sidecar = MakePhysicsSidecarSpec(seed, asset);
    const auto source_assets = std::array { asset, material_asset, sidecar };
    require(WriteLooseIndex(source_root, source_assets, files, seed));
    result.sources.push_back(
      { .kind = data::CookedSourceKind::kLooseCooked, .path = source_root });
  }
  return result;
}

//! The full build of the scene mask sources.
[[nodiscard]] inline auto MakeSceneMaskRequest(
  const std::filesystem::path& output_pak_path,
  std::vector<data::CookedSource> sources) -> PakBuildRequest
{
  return MakeFullRequest(output_pak_path,
    {
      .sources = std::move(sources),
      .content_version = 1U,
      .source_key = MakeSourceKey(3U),
    });
}

//! A pak packed from the scene mask sources, ready to be patched or repacked.
struct SceneMaskPak final {
  PakBuildRequest request;
  data::PakCatalog catalog;
};

//! Plans and writes `root/masks.pak`; throws `std::runtime_error` on failure.
[[nodiscard]] inline auto BuildSceneMaskPak(const std::filesystem::path& root)
  -> SceneMaskPak
{
  auto request = MakeSceneMaskRequest(
    root / "masks.pak", WriteSceneMaskSources(root).sources);
  const auto planned = PakPlanBuilder {}.Build(request);
  if (HasError(planned.diagnostics) || !planned.plan.has_value()) {
    throw std::runtime_error("cannot plan the scene mask pak: "
      + oxygen::cooker::test::DiagnosticSummary(planned.diagnostics));
  }
  const auto written = PakWriter {}.Write(request, *planned.plan);
  if (HasError(written.diagnostics)) {
    throw std::runtime_error("cannot write the scene mask pak: "
      + oxygen::cooker::test::DiagnosticSummary(written.diagnostics));
  }
  return SceneMaskPak {
    .request = std::move(request),
    .catalog = planned.output_catalog,
  };
}

} // namespace oxygen::content::pak::test
