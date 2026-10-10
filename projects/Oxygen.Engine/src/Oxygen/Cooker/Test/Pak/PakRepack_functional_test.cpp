//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/PakPlanBuilder.cpp, Pak/PakWriter.cpp

// Covers: Pak/PakBuilder.cpp, Pak/PakPlanBuilder.cpp

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <string>
#include <vector>

#include "PakSceneTestSupport.h"
#include "PakTestSupport.h"

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Cooker/Pak/PakBuildReport.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakPlan.h>
#include <Oxygen/Cooker/Pak/PakPlanBuilder.h>
#include <Oxygen/Cooker/Pak/PakWriter.h>
#include <Oxygen/Cooker/Test/Support/DescriptorFixtures.h>
#include <Oxygen/Cooker/Test/Support/Diagnostics.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormatSerioLoaders.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace cooktest = oxygen::cooker::test;
namespace data = oxygen::data;
namespace lc = oxygen::data::loose_cooked;
namespace pak = oxygen::content::pak;
namespace paktest = oxygen::content::pak::test;
namespace core = oxygen::data::pak::core;
namespace render = oxygen::data::pak::render;
namespace script = oxygen::data::pak::scripting;

using paktest::AssetSpec;
using paktest::BuildSceneDescriptorWithScriptingSlots;
using paktest::FileSpec;
using paktest::HasDiagnosticCode;
using paktest::HasError;
using paktest::MakeAssetKey;
using paktest::MakeBaseCatalog;
using paktest::MakeSceneScriptAssets;
using paktest::MakeSourceKey;

auto ExpectDescriptorHash(const std::span<const std::byte> bytes) -> void
{
  auto header = core::AssetHeader {};
  ASSERT_GE(bytes.size(), sizeof(header));
  std::memcpy(&header, bytes.data(), sizeof(header));
  auto unhashed = std::vector<std::byte>(bytes.begin(), bytes.end());
  std::ranges::fill(
    std::span(unhashed).subspan(
      offsetof(core::AssetHeader, content_hash), sizeof(header.content_hash)),
    std::byte { 0 });
  EXPECT_EQ(header.content_hash, oxygen::base::ComputeSha256(unhashed));
}

//! Packing from existing paks must keep every binding, resource and
//! descriptor intact or refuse the input.
class PakRepackTest : public paktest::TempDirFixture { };

NOLINT_TEST_F(
  PakRepackTest, RepackingRejectsDescriptorBytesThatDisagreeWithCatalog)
{
  const auto root = Root() / "source";
  const auto bytes = oxygen::content::test::MakeEmptySceneDescriptor();
  const auto asset = AssetSpec {
    .key = MakeAssetKey(1U),
    .asset_type = data::AssetType::kScene,
    .descriptor_relpath = "Scene.oscene",
    .virtual_path = "/Game/Scene.oscene",
    .descriptor_size = bytes.size(),
    .descriptor_sha = oxygen::base::ComputeSha256(bytes),
    .descriptor_payload = bytes,
    .references = {},
  };
  ASSERT_TRUE(paktest::WriteLooseIndex(root, std::span { &asset, 1U }, {}, 1U));
  auto request = pak::PakBuildRequest {
    .mode = pak::BuildMode::kFull,
    .sources
    = { { .kind = data::CookedSourceKind::kLooseCooked, .path = root } },
    .output_pak_path = Root() / "source.pak",
    .content_version = 1U,
    .source_key = MakeSourceKey(1U),
  };
  const auto original = pak::PakPlanBuilder {}.Build(request);
  ASSERT_TRUE(original.plan.has_value());
  ASSERT_FALSE(
    HasError(pak::PakWriter {}.Write(request, *original.plan).diagnostics));
  {
    std::fstream archive(
      request.output_pak_path, std::ios::binary | std::ios::in | std::ios::out);
    const auto offset = CheckedAt(original.plan->Assets(), 0U).offset
      + offsetof(core::AssetHeader, name);
    archive.seekp(static_cast<std::streamoff>(offset));
    archive.put('X');
    archive.flush();
    ASSERT_TRUE(archive.good());
  }
  request.sources = { { .kind = data::CookedSourceKind::kPak,
    .path = request.output_pak_path } };
  request.output_pak_path = Root() / "repacked.pak";
  request.source_key = MakeSourceKey(2U);
  const auto rejected = pak::PakPlanBuilder {}.Build(request);
  EXPECT_FALSE(rejected.plan.has_value());
  EXPECT_TRUE(HasDiagnosticCode(
    rejected.diagnostics, "pak.plan.catalog_content_mismatch"));
}

NOLINT_TEST_F(PakRepackTest, RepackPreservesSceneBindings)
{
  namespace serio = oxygen::serio;
  const auto source_pak = paktest::BuildSceneMaskPak(Root());

  for (const auto mode : { pak::BuildMode::kFull, pak::BuildMode::kPatch }) {
    auto repack = source_pak.request;
    repack.mode = mode;
    repack.sources = {
      {
        .kind = data::CookedSourceKind::kPak,
        .path = source_pak.request.output_pak_path,
      },
    };
    repack.output_pak_path
      = Root() / (mode == pak::BuildMode::kFull ? "repacked.pak" : "patch.pak");
    repack.source_key = MakeSourceKey(4U);
    if (mode == pak::BuildMode::kPatch) {
      repack.output_manifest_path = Root() / "patch.manifest.json";
      repack.base_catalogs.push_back(data::PakCatalog {
        .source_key = MakeSourceKey(5U),
        .content_version = 1U,
        .catalog_digest = paktest::MakeDigest(5U),
        .entries = {},
      });
      auto& base = repack.base_catalogs.at(0);
      base.catalog_digest = base.ComputeDigest().value();
    }
    const auto planned = pak::PakPlanBuilder {}.Build(repack);
    for (const auto& diagnostic : planned.diagnostics) {
      EXPECT_NE(diagnostic.severity, pak::PakDiagnosticSeverity::kError)
        << diagnostic.message;
    }
    ASSERT_TRUE(planned.plan.has_value());
    ASSERT_FALSE(
      HasError(pak::PakWriter {}.Write(repack, *planned.plan).diagnostics));
    auto archive = oxygen::content::PakFile(repack.output_pak_path);
    archive.ValidateCrc32Integrity();
    for (const auto seed : { uint8_t { 1U }, uint8_t { 2U } }) {
      const auto entry
        = archive.FindEntry(MakeAssetKey(static_cast<uint8_t>(seed * 10U)));
      ASSERT_TRUE(entry.has_value());
      auto descriptor_reader = archive.CreateReader(*entry);
      const auto bytes = descriptor_reader.ReadBlob(entry->desc_size);
      ASSERT_TRUE(bytes.has_value());
      const auto scene = data::SceneAsset(entry->asset_key, *bytes);
      ExpectDescriptorHash(*bytes);
      const auto sidecar_entry = archive.FindEntry(MakeAssetKey(seed));
      ASSERT_TRUE(sidecar_entry.has_value());
      auto sidecar_reader = archive.CreateReader(*sidecar_entry);
      const auto sidecar_bytes
        = sidecar_reader.ReadBlob(sidecar_entry->desc_size);
      ASSERT_TRUE(sidecar_bytes.has_value());
      const auto sidecar = data::PhysicsSceneAsset(
        sidecar_entry->asset_key, std::span<const std::byte>(*sidecar_bytes));
      EXPECT_EQ(sidecar.GetTargetSceneKey(), entry->asset_key);
      EXPECT_TRUE(std::ranges::equal(sidecar.GetTargetSceneContentHash(),
        oxygen::base::ComputeSha256(*bytes)));
      ExpectDescriptorHash(*sidecar_bytes);
      const auto post_process = scene.TryGetPostProcessVolumeEnvironment();
      ASSERT_TRUE(post_process.has_value());
      const auto expected_index = seed == 1U ? 1U
        : mode == pak::BuildMode::kFull      ? 3U
                                             : 2U;
      EXPECT_EQ(post_process->auto_exposure_metering_mask.get(), 0U);
      const auto scene_references
        = archive.ReadAssetReferences(entry->asset_key);
      const auto texture_binding = scene_references.ResolveResource(
        post_process->auto_exposure_metering_mask,
        data::ResourceKind::kTexture);
      ASSERT_TRUE(texture_binding.has_value());
      ASSERT_TRUE(texture_binding->has_value());
      EXPECT_EQ((**texture_binding).get(), expected_index);
      const auto texture_offset
        = archive.TexturesTable().GetResourceOffset(**texture_binding);
      ASSERT_TRUE(texture_offset.has_value());
      serio::FileStream<> stream(repack.output_pak_path, std::ios::in);
      serio::Reader reader(stream);
      ASSERT_TRUE(reader.Seek(*texture_offset));
      auto texture = core::TextureResourceDesc {};
      ASSERT_TRUE(serio::Load(reader, texture));
      ASSERT_TRUE(reader.Seek(texture.data_offset));
      const auto payload = reader.ReadBlob(texture.size_bytes);
      ASSERT_TRUE(payload.has_value());
      EXPECT_EQ(
        *payload, std::vector<std::byte>(4U, static_cast<std::byte>(seed)));
      const auto material_entry
        = archive.FindEntry(MakeAssetKey(static_cast<uint8_t>(seed + 10U)));
      ASSERT_TRUE(material_entry.has_value());
      auto material_reader = archive.CreateReader(*material_entry);
      auto material = render::MaterialAssetDesc {};
      const auto material_payload = material_reader.ReadBlob(sizeof(material));
      ASSERT_TRUE(material_payload.has_value());
      ExpectDescriptorHash(*material_payload);
      std::memcpy(&material, material_payload->data(), sizeof(material));
      EXPECT_EQ(material.base_color_texture.get(), 0U);
      const auto material_references
        = archive.ReadAssetReferences(material_entry->asset_key);
      const auto material_binding = material_references.ResolveResource(
        material.base_color_texture, data::ResourceKind::kTexture);
      ASSERT_TRUE(material_binding.has_value());
      ASSERT_TRUE(material_binding->has_value());
      EXPECT_EQ((**material_binding).get(), expected_index);
    }
  }
}

NOLINT_TEST_F(PakRepackTest, ScriptBindingsSurviveSourceMergeAndPakRepacking)
{
  constexpr uint32_t kScriptKeyStride = 10U;
  constexpr int32_t kParameterValueStride = 100;
  auto sources = std::vector<data::CookedSource> {};
  for (const auto seed : { uint8_t { 1U }, uint8_t { 2U } }) {
    const auto root = Root() / std::to_string(seed);
    const auto assets = MakeSceneScriptAssets(seed);
    ASSERT_TRUE(paktest::WriteLooseIndex(root, assets, {}, seed));
    sources.push_back(
      { .kind = data::CookedSourceKind::kLooseCooked, .path = root });
  }
  auto request = paktest::MakeFullRequest(Root() / "scripts.pak",
    { .sources = sources, .source_key = MakeSourceKey(3U) });
  const auto verify = [](const std::filesystem::path& path) -> void {
    SCOPED_TRACE(path.string());
    auto archive = oxygen::content::PakFile(path);
    archive.ValidateCrc32Integrity();
    for (const auto seed : { uint8_t { 1U }, uint8_t { 2U } }) {
      const auto entry = archive.FindEntry(MakeAssetKey(seed));
      ASSERT_TRUE(entry.has_value());
      auto reader = archive.CreateReader(*entry);
      const auto bytes = reader.ReadBlob(entry->desc_size);
      ASSERT_TRUE(bytes.has_value());
      EXPECT_EQ(*bytes, BuildSceneDescriptorWithScriptingSlots(seed));
      const auto scene = data::SceneAsset(entry->asset_key, *bytes);
      const auto bindings
        = scene.GetComponents<script::ScriptingComponentRecord>();
      ASSERT_EQ(bindings.size(), 2U);
      for (uint32_t index = 0U; index < bindings.size(); ++index) {
        EXPECT_EQ(
          oxygen::base::CheckedAt(bindings, index).slot_start_index, index);
        const auto slots = scene.ReadScriptSlots(
          oxygen::base::CheckedAt(bindings, index).slot_start_index, 1U);
        ASSERT_EQ(slots.size(), 1U);
        EXPECT_EQ(slots.at(0).script_asset_key,
          MakeAssetKey(static_cast<uint8_t>(
            (static_cast<uint32_t>(seed) * kScriptKeyStride)
            + static_cast<uint32_t>(index))));
        const auto params = scene.ReadScriptParameters(slots.front());
        ASSERT_EQ(params.size(), 1U);
        EXPECT_EQ(params.at(0).value.as_int32,
          (seed * kParameterValueStride) + static_cast<int32_t>(index));
      }
    }
  };
  const auto build_and_verify
    = [&verify](const pak::PakBuildRequest& build_request) -> void {
    const auto planned = pak::PakPlanBuilder {}.Build(build_request);
    ASSERT_FALSE(HasError(planned.diagnostics))
      << cooktest::DiagnosticSummary(planned.diagnostics);
    ASSERT_TRUE(planned.plan.has_value());
    const auto written = pak::PakWriter {}.Write(build_request, *planned.plan);
    ASSERT_FALSE(HasError(written.diagnostics))
      << cooktest::DiagnosticSummary(written.diagnostics);
    verify(build_request.output_pak_path);
  };
  build_and_verify(request);
  const auto original_pak = request.output_pak_path;
  for (const auto kind :
    { data::CookedSourceKind::kLooseCooked, data::CookedSourceKind::kPak }) {
    request.sources = kind == data::CookedSourceKind::kLooseCooked
      ? sources
      : std::vector<data::CookedSource> {
          {
            .kind = kind,
            .path = original_pak,
          },
        };
    for (const auto mode : { pak::BuildMode::kFull, pak::BuildMode::kPatch }) {
      request.mode = mode;
      request.output_pak_path = Root()
        / (std::to_string(static_cast<int>(kind)) + "-"
          + std::to_string(static_cast<int>(mode)) + ".pak");
      request.output_manifest_path
        = request.output_pak_path.string() + ".manifest.json";
      request.base_catalogs = { MakeBaseCatalog({}) };
      build_and_verify(request);
    }
  }
}

NOLINT_TEST_F(PakRepackTest, BufferAndScriptReferencesSurvivePakRepacking)
{
  namespace geometry = oxygen::data::pak::geometry;
  auto sources = std::vector<data::CookedSource> {};
  for (const auto seed : { uint8_t { 1U }, uint8_t { 2U } }) {
    const auto root = Root() / std::to_string(seed);
    auto geometry_desc = geometry::GeometryAssetDesc {};
    geometry_desc.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kGeometry);
    geometry_desc.header.version = geometry::kGeometryAssetVersion;
    geometry_desc.lod_count = 1U;
    auto mesh = geometry::MeshDesc {};
    mesh.mesh_type = static_cast<uint8_t>(data::MeshType::kStandard);
    mesh.info.standard.vertex_buffer = data::ResourceReferenceIndex { 0U };
    mesh.info.standard.index_buffer = data::ResourceReferenceIndex { 0U };
    const auto descriptor_bytes
      = std::as_bytes(std::span { &geometry_desc, 1U });
    const auto mesh_bytes = std::as_bytes(std::span { &mesh, 1U });
    auto geometry_bytes = std::vector<std::byte>(
      descriptor_bytes.begin(), descriptor_bytes.end());
    geometry_bytes.insert(
      geometry_bytes.end(), mesh_bytes.begin(), mesh_bytes.end());
    auto script_desc = script::ScriptAssetDesc {};
    script_desc.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kScript);
    script_desc.header.version = script::kScriptAssetVersion;
    script_desc.bytecode_resource_index = data::ResourceReferenceIndex { 0U };
    const auto script_bytes = std::as_bytes(std::span { &script_desc, 1U });
    constexpr uint8_t kScriptKeyOffset = 10U;
    const auto assets = std::array {
      AssetSpec {
        .key = MakeAssetKey(seed),
        .asset_type = data::AssetType::kGeometry,
        .descriptor_relpath = "Mesh.ogeo",
        .virtual_path = "/Game/Mesh" + std::to_string(seed) + ".ogeo",
        .descriptor_size = geometry_bytes.size(),
        .descriptor_sha = oxygen::base::ComputeSha256(geometry_bytes),
        .descriptor_payload = geometry_bytes,
        .references = data::AssetReferences::Create(
          {
            { .kind = data::ResourceKind::kBuffer,
              .index = oxygen::ResourceIndexT { 1U } },
          },
          {})
          .value(),
      },
      AssetSpec {
        .key = MakeAssetKey(static_cast<uint8_t>(seed + kScriptKeyOffset)),
        .asset_type = data::AssetType::kScript,
        .descriptor_relpath = "Logic.oscript",
        .virtual_path = "/Game/Logic" + std::to_string(seed) + ".oscript",
        .descriptor_size = script_bytes.size(),
        .descriptor_sha = oxygen::base::ComputeSha256(script_bytes),
        .descriptor_payload = { script_bytes.begin(), script_bytes.end() },
        .references = data::AssetReferences::Create(
          {
            { .kind = data::ResourceKind::kScript,
              .index = oxygen::ResourceIndexT { 1U } },
          },
          {})
          .value(),
      },
    };
    auto buffers = std::array<core::BufferResourceDesc, 2> {};
    buffers.back().size_bytes = sizeof(uint32_t);
    auto scripts = std::array<script::ScriptResourceDesc, 2> {};
    scripts.back().size_bytes = sizeof(uint32_t);
    const auto buffer_table = std::as_bytes(std::span(buffers));
    const auto script_table = std::as_bytes(std::span(scripts));
    const auto payload
      = std::vector<std::byte>(sizeof(uint32_t), static_cast<std::byte>(seed));
    const auto files = std::array {
      FileSpec {
        .kind = lc::FileKind::kBuffersTable,
        .relpath = "buffers.table",
        .payload = { buffer_table.begin(), buffer_table.end() },
      },
      FileSpec {
        .kind = lc::FileKind::kBuffersData,
        .relpath = "buffers.data",
        .payload = payload,
      },
      FileSpec {
        .kind = lc::FileKind::kScriptsTable,
        .relpath = "scripts.table",
        .payload = { script_table.begin(), script_table.end() },
      },
      FileSpec {
        .kind = lc::FileKind::kScriptsData,
        .relpath = "scripts.data",
        .payload = payload,
      },
    };
    ASSERT_TRUE(paktest::WriteLooseIndex(root, assets, files, seed));
    sources.push_back(
      { .kind = data::CookedSourceKind::kLooseCooked, .path = root });
  }
  auto request = paktest::MakeFullRequest(Root() / "resources.pak",
    { .sources = sources, .source_key = MakeSourceKey(3U) });
  const auto initial = pak::PakPlanBuilder {}.Build(request);
  ASSERT_TRUE(initial.plan) << cooktest::DiagnosticSummary(initial.diagnostics);
  ASSERT_FALSE(
    HasError(pak::PakWriter {}.Write(request, *initial.plan).diagnostics));
  const auto source_pak = request.output_pak_path;
  for (const auto mode : { pak::BuildMode::kFull, pak::BuildMode::kPatch }) {
    request.mode = mode;
    request.sources
      = { { .kind = data::CookedSourceKind::kPak, .path = source_pak } };
    request.output_pak_path
      = Root() / (mode == pak::BuildMode::kFull ? "full.pak" : "patch.pak");
    request.output_manifest_path = Root() / "patch.manifest.json";
    request.base_catalogs = { MakeBaseCatalog({}) };
    const auto planned = pak::PakPlanBuilder {}.Build(request);
    ASSERT_TRUE(planned.plan)
      << cooktest::DiagnosticSummary(planned.diagnostics);
    ASSERT_FALSE(
      HasError(pak::PakWriter {}.Write(request, *planned.plan).diagnostics));
    auto archive = oxygen::content::PakFile(request.output_pak_path);
    archive.ValidateCrc32Integrity();
    for (const auto seed : { uint8_t { 1U }, uint8_t { 2U } }) {
      const auto second_source_index = mode == pak::BuildMode::kFull ? 3U : 2U;
      const auto expected = seed == 1U ? 1U : second_source_index;
      const auto entry = archive.FindEntry(MakeAssetKey(seed));
      ASSERT_TRUE(entry);
      auto reader = archive.CreateReader(*entry);
      ASSERT_TRUE(reader.ReadBlob(sizeof(geometry::GeometryAssetDesc)));
      const auto mesh_bytes = reader.ReadBlob(sizeof(geometry::MeshDesc));
      ASSERT_TRUE(mesh_bytes);
      auto mesh = geometry::MeshDesc {};
      std::memcpy(&mesh, mesh_bytes->data(), sizeof(mesh));
      EXPECT_EQ(mesh.info.standard.vertex_buffer.get(), 0U);
      EXPECT_EQ(mesh.info.standard.index_buffer.get(), 0U);
      const auto geometry_references
        = archive.ReadAssetReferences(entry->asset_key);
      const auto buffer_binding = geometry_references.ResolveResource(
        mesh.info.standard.vertex_buffer, data::ResourceKind::kBuffer);
      ASSERT_TRUE(buffer_binding.has_value());
      ASSERT_TRUE(buffer_binding->has_value());
      EXPECT_EQ((**buffer_binding).get(), expected);
      constexpr uint8_t kScriptKeyOffset = 10U;
      const auto script_entry = archive.FindEntry(
        MakeAssetKey(static_cast<uint8_t>(seed + kScriptKeyOffset)));
      ASSERT_TRUE(script_entry);
      auto script_reader = archive.CreateReader(*script_entry);
      const auto script_bytes
        = script_reader.ReadBlob(sizeof(script::ScriptAssetDesc));
      ASSERT_TRUE(script_bytes);
      auto script_desc = script::ScriptAssetDesc {};
      std::memcpy(&script_desc, script_bytes->data(), sizeof(script_desc));
      EXPECT_EQ(script_desc.bytecode_resource_index.get(), 0U);
      const auto script_references
        = archive.ReadAssetReferences(script_entry->asset_key);
      const auto script_binding = script_references.ResolveResource(
        script_desc.bytecode_resource_index, data::ResourceKind::kScript);
      ASSERT_TRUE(script_binding.has_value());
      ASSERT_TRUE(script_binding->has_value());
      EXPECT_EQ((**script_binding).get(), expected);
      oxygen::serio::FileStream<> stream(request.output_pak_path, std::ios::in);
      oxygen::serio::Reader payload_reader(stream);
      const auto buffer_offset
        = archive.BuffersTable().GetResourceOffset(**buffer_binding);
      ASSERT_TRUE(buffer_offset);
      ASSERT_TRUE(payload_reader.Seek(*buffer_offset));
      auto buffer = core::BufferResourceDesc {};
      ASSERT_TRUE(oxygen::serio::Load(payload_reader, buffer));
      ASSERT_TRUE(payload_reader.Seek(buffer.data_offset));
      const auto bytes = payload_reader.ReadBlob(buffer.size_bytes);
      ASSERT_TRUE(bytes);
      EXPECT_EQ(*bytes,
        std::vector<std::byte>(sizeof(uint32_t), static_cast<std::byte>(seed)));
    }
  }
}

} // namespace
