//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/PakPlanBuilder.cpp, Pak/PakWriter.cpp

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "PakSceneTestSupport.h"

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/testing/Descriptors.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/PackedPak.h>
#include <Oxygen/Cooker/testing/PakWorkspace.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace data = oxygen::data;
namespace pak = oxygen::content::pak;
namespace core = oxygen::data::pak::core;
namespace geometry = oxygen::data::pak::geometry;
namespace render = oxygen::data::pak::render;
namespace script = oxygen::data::pak::scripting;

using oxygen::ResourceIndexT;
using oxygen::cooker::test::HasDiagnostic;
using oxygen::cooker::test::HasNoErrors;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::PackedPak;
using oxygen::cooker::testing::PakWorkspace;
using oxygen::cooker::testing::SeedBytes;
using oxygen::serio::testing::BytesEqual;
using pak::BuildMode;

//! Every fixture packs two libraries, seeded 1 and 2. A pak reserves resource
//! row zero once, so each library's one resource lands at its own seed.
constexpr auto kSeeds = std::array { uint8_t { 1U }, uint8_t { 2U } };
constexpr auto kModes = std::array { BuildMode::kFull, BuildMode::kPatch };

auto ModeName(const BuildMode mode) -> std::string_view
{
  return mode == BuildMode::kFull ? "Full" : "Patch";
}

//! Packs `sources` into `merged.pak`, then repacks that pak in `mode`: in
//! full, or as a patch over an empty base of the same library.
auto Repack(PakWorkspace& workspace, std::vector<data::CookedSource> sources,
  const BuildMode mode) -> PackedPak
{
  const auto merged = workspace.FullRequest(std::move(sources), "merged.pak");
  static_cast<void>(workspace.PackOrThrow(merged));
  auto from_pak
    = std::vector { PakWorkspace::PakSource(merged.output_pak_path) };
  return workspace.PackOrThrow(mode == BuildMode::kFull
      ? workspace.FullRequest(std::move(from_pak), "repacked.pak")
      : workspace.PatchRequest(std::move(from_pak),
          { LooseCookedRoot {}.BaseCatalog() }, "repacked.pak"));
}

//! Checks the content hash a descriptor's header holds against its bytes.
auto ExpectDescriptorHash(const std::span<const std::byte> bytes) -> void
{
  const auto header = PackedPak::RecordAt<core::AssetHeader>(bytes, 0);
  auto unhashed = std::vector<std::byte>(bytes.begin(), bytes.end());
  std::ranges::fill(
    std::span(unhashed).subspan(
      offsetof(core::AssetHeader, content_hash), sizeof(header.content_hash)),
    std::byte { 0 });
  EXPECT_EQ(header.content_hash, oxygen::base::ComputeSha256(unhashed));
}

//=== A pak whose bytes disagree with its catalog ===-------------------------//

NOLINT_TEST(PakRepackTest, RejectsDescriptorBytesThatDisagreeWithTheCatalog)
{
  auto workspace = PakWorkspace {};
  auto game = LooseCookedRoot {};
  game.Add({
    .type = data::AssetType::kScene,
    .leaf = "Scene.oscene",
    .descriptor = oxygen::cooker::testing::MakeEmptySceneDescriptor(),
  });
  const auto request = workspace.FullRequest({ workspace.Add(game) });
  const auto packed = workspace.Pack(request);
  ASSERT_THAT(packed.Diagnostics(), HasNoErrors());
  // Corrupt the descriptor's variant flags after the pak was published.
  const auto published = workspace.Storage().Bytes(request.output_pak_path);
  auto bytes = std::vector<std::byte>(published.begin(), published.end());
  bytes.at(oxygen::base::CheckedAt(packed.planned.plan->Assets(), 0U).offset
    + offsetof(core::AssetHeader, variant_flags)) = std::byte { 'X' };
  workspace.Storage().AddFile(request.output_pak_path, bytes);

  const auto rejected = workspace.Pack(workspace.FullRequest(
    { PakWorkspace::PakSource(request.output_pak_path) }, "repacked.pak"));

  EXPECT_FALSE(rejected.planned.plan.has_value());
  EXPECT_THAT(
    rejected.Diagnostics(), HasDiagnostic("pak.plan.catalog_content_mismatch"));
}

//=== Scenes binding a texture through a mask ===-----------------------------//

class PakRepackSceneTest : public ::testing::TestWithParam<BuildMode> {
protected:
  //! The two scene mask libraries, merged then repacked.
  auto Repacked() -> PackedPak
  {
    auto sources = std::vector<data::CookedSource> {};
    for (const auto seed : kSeeds) {
      sources.push_back(workspace_.Add(pak::test::MakeSceneMask(seed).root));
    }
    return Repack(workspace_, std::move(sources), GetParam());
  }

private:
  PakWorkspace workspace_;
};

NOLINT_TEST_P(PakRepackSceneTest, KeepsDescriptorHashes)
{
  const auto pak = Repacked();

  for (const auto seed : kSeeds) {
    SCOPED_TRACE(int { seed });
    const auto mask = pak::test::MakeSceneMask(seed);
    ExpectDescriptorHash(pak.Descriptor(mask.scene));
    ExpectDescriptorHash(pak.Descriptor(mask.sidecar));
    ExpectDescriptorHash(pak.Descriptor(mask.material));
  }
}

NOLINT_TEST_P(PakRepackSceneTest, KeepsPhysicsSidecarTargets)
{
  const auto pak = Repacked();

  for (const auto seed : kSeeds) {
    SCOPED_TRACE(int { seed });
    const auto mask = pak::test::MakeSceneMask(seed);
    const auto sidecar
      = data::PhysicsSceneAsset(mask.sidecar, pak.Descriptor(mask.sidecar));
    EXPECT_EQ(sidecar.GetTargetSceneKey(), mask.scene);
    EXPECT_TRUE(std::ranges::equal(sidecar.GetTargetSceneContentHash(),
      oxygen::base::ComputeSha256(pak.Descriptor(mask.scene))));
  }
}

NOLINT_TEST_P(PakRepackSceneTest, RebindsSceneMeteringMasks)
{
  const auto pak = Repacked();

  for (const auto seed : kSeeds) {
    SCOPED_TRACE(int { seed });
    const auto mask = pak::test::MakeSceneMask(seed);
    const auto scene = data::SceneAsset(mask.scene, pak.Descriptor(mask.scene));
    const auto post_process = scene.TryGetPostProcessVolumeEnvironment();
    ASSERT_HAS_VALUE(post_process);
    const auto texture = pak.Resolve(mask.scene,
      post_process->auto_exposure_metering_mask, data::ResourceKind::kTexture);
    ASSERT_HAS_VALUE(texture);
    EXPECT_EQ(texture->get(), seed);
    EXPECT_THAT(pak.Payload<data::TextureResource>(*texture),
      BytesEqual(mask.texture_payload));
  }
}

NOLINT_TEST_P(PakRepackSceneTest, RebindsMaterialTextures)
{
  const auto pak = Repacked();

  for (const auto seed : kSeeds) {
    SCOPED_TRACE(int { seed });
    const auto mask = pak::test::MakeSceneMask(seed);
    const auto material
      = pak.DescriptorRecord<render::MaterialAssetDesc>(mask.material);
    const auto texture = pak.Resolve(
      mask.material, material.base_color_texture, data::ResourceKind::kTexture);
    ASSERT_HAS_VALUE(texture);
    EXPECT_EQ(texture->get(), seed);
  }
}

INSTANTIATE_TEST_SUITE_P(Modes, PakRepackSceneTest, ::testing::ValuesIn(kModes),
  [](const auto& info) { return std::string(ModeName(info.param)); });

//=== Geometry and scripts binding resources ===------------------------------//

//! The library of `seed`: a geometry whose mesh binds a buffer, and a script
//! that binds its bytecode; both payloads are `SeedBytes(seed)`.
struct ResourceLibrary final {
  LooseCookedRoot library;
  data::AssetKey geometry;
  data::AssetKey script;
};

auto MakeResourceRoot(const uint8_t seed) -> ResourceLibrary
{
  auto library = LooseCookedRoot { "Game", seed };
  const auto binding
    = [](const data::ResourceKind kind, const ResourceIndexT index) {
        return data::AssetReferences::Create(
          { { .kind = kind, .index = index } }, {})
          .value();
      };

  auto desc = geometry::GeometryAssetDesc {};
  desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kGeometry);
  desc.header.version = geometry::kGeometryAssetVersion;
  desc.lod_count = 1U;
  auto mesh = geometry::MeshDesc {};
  mesh.mesh_type = static_cast<uint8_t>(data::MeshType::kStandard);
  mesh.info.standard.vertex_buffer = data::ResourceReferenceIndex { 0U };
  mesh.info.standard.index_buffer = data::ResourceReferenceIndex { 0U };
  auto geometry_bytes = std::vector<std::byte> {};
  oxygen::cooker::testing::AppendRecord(geometry_bytes, desc);
  oxygen::cooker::testing::AppendRecord(geometry_bytes, mesh);
  data::AppendDescriptorStrings(
    geometry_bytes, data::DescriptorStringsBuilder {});
  const auto buffer = library.AddBuffer(SeedBytes(seed));
  const auto geometry = library.Add({
    .type = data::AssetType::kGeometry,
    .leaf = "Mesh" + std::to_string(seed) + ".ogeo",
    .descriptor = std::move(geometry_bytes),
    .references = binding(data::ResourceKind::kBuffer, buffer),
  });

  auto script_desc = script::ScriptAssetDesc {};
  script_desc.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kScript);
  script_desc.header.version = script::kScriptAssetVersion;
  script_desc.bytecode_resource_index = data::ResourceReferenceIndex { 0U };
  const auto bytecode = library.AddScript(SeedBytes(seed));
  const auto logic = library.Add({
    .type = data::AssetType::kScript,
    .leaf = "Logic" + std::to_string(seed) + ".oscript",
    .descriptor = data::MakeDescriptorBytes(script_desc),
    .references = binding(data::ResourceKind::kScript, bytecode),
  });
  return {
    .library = std::move(library),
    .geometry = geometry,
    .script = logic,
  };
}

class PakRepackResourceTest : public ::testing::TestWithParam<BuildMode> {
protected:
  auto Repacked() -> PackedPak
  {
    auto sources = std::vector<data::CookedSource> {};
    for (const auto seed : kSeeds) {
      sources.push_back(workspace_.Add(MakeResourceRoot(seed).library));
    }
    return Repack(workspace_, std::move(sources), GetParam());
  }

private:
  PakWorkspace workspace_;
};

NOLINT_TEST_P(PakRepackResourceTest, RebindsGeometryBuffers)
{
  const auto pak = Repacked();

  for (const auto seed : kSeeds) {
    SCOPED_TRACE(int { seed });
    const auto key = MakeResourceRoot(seed).geometry;
    const auto mesh = pak.DescriptorRecord<geometry::MeshDesc>(
      key, sizeof(geometry::GeometryAssetDesc));
    const auto buffer = pak.Resolve(
      key, mesh.info.standard.vertex_buffer, data::ResourceKind::kBuffer);
    ASSERT_HAS_VALUE(buffer);
    EXPECT_EQ(buffer->get(), seed);
    EXPECT_THAT(
      pak.Payload<data::BufferResource>(*buffer), BytesEqual(SeedBytes(seed)));
  }
}

NOLINT_TEST_P(PakRepackResourceTest, RebindsScriptBytecode)
{
  const auto pak = Repacked();

  for (const auto seed : kSeeds) {
    SCOPED_TRACE(int { seed });
    const auto key = MakeResourceRoot(seed).script;
    const auto desc = pak.DescriptorRecord<script::ScriptAssetDesc>(key);
    const auto bytecode = pak.Resolve(
      key, desc.bytecode_resource_index, data::ResourceKind::kScript);
    ASSERT_HAS_VALUE(bytecode);
    EXPECT_EQ(bytecode->get(), seed);
  }
}

INSTANTIATE_TEST_SUITE_P(Modes, PakRepackResourceTest,
  ::testing::ValuesIn(kModes),
  [](const auto& info) { return std::string(ModeName(info.param)); });

//=== Scenes binding scripts ===----------------------------------------------//

//! Packs the scene script libraries, merged from their loose roots or
//! repacked from a pak of them, in full or as a patch.
class PakRepackScriptTest
  : public ::testing::TestWithParam<std::tuple<bool, BuildMode>> {
protected:
  auto Packed() -> PackedPak
  {
    const auto from_pak = std::get<0>(GetParam());
    const auto mode = std::get<1>(GetParam());
    auto sources = std::vector<data::CookedSource> {};
    for (const auto seed : kSeeds) {
      sources.push_back(workspace_.Add(pak::test::MakeSceneScripts(seed).root));
    }
    if (from_pak) {
      return Repack(workspace_, std::move(sources), mode);
    }
    return workspace_.PackOrThrow(mode == BuildMode::kFull
        ? workspace_.FullRequest(std::move(sources))
        : workspace_.PatchRequest(
            std::move(sources), { LooseCookedRoot {}.BaseCatalog() }));
  }

private:
  PakWorkspace workspace_;
};

NOLINT_TEST_P(PakRepackScriptTest, KeepsSceneDescriptorsVerbatim)
{
  const auto pak = Packed();

  for (const auto seed : kSeeds) {
    SCOPED_TRACE(int { seed });
    EXPECT_THAT(pak.Descriptor(pak::test::MakeSceneScripts(seed).scene),
      BytesEqual(pak::test::BuildSceneDescriptorWithScriptingSlots(seed)));
  }
}

NOLINT_TEST_P(PakRepackScriptTest, KeepsScriptSlotsAndParameters)
{
  const auto pak = Packed();

  for (const auto seed : kSeeds) {
    const auto key = pak::test::MakeSceneScripts(seed).scene;
    const auto scene = data::SceneAsset(key, pak.Descriptor(key));
    const auto bindings
      = scene.GetComponents<script::ScriptingComponentRecord>();
    ASSERT_EQ(bindings.size(), 2U);
    for (uint8_t index = 0U; index < bindings.size(); ++index) {
      SCOPED_TRACE(::testing::Message()
        << "seed " << int { seed } << " binding " << int { index });
      const auto& binding = oxygen::base::CheckedAt(bindings, index);
      EXPECT_EQ(binding.slot_start_index, index);
      const auto slots = scene.ReadScriptSlots(binding.slot_start_index, 1U);
      ASSERT_EQ(slots.size(), 1U);
      EXPECT_EQ(
        slots.front().script_asset_key, pak::test::ScriptKey(seed, index));
      const auto params = scene.ReadScriptParameters(slots.front());
      ASSERT_EQ(params.size(), 1U);
      EXPECT_EQ(
        params.front().value.as_int32, pak::test::ScriptParameter(seed, index));
    }
  }
}

INSTANTIATE_TEST_SUITE_P(SourcesAndModes, PakRepackScriptTest,
  ::testing::Combine(::testing::Bool(), ::testing::ValuesIn(kModes)),
  [](const auto& info) {
    // No structured binding: its comma would split the macro's arguments.
    return std::string(std::get<0>(info.param) ? "Pak" : "Loose")
      .append(ModeName(std::get<1>(info.param)));
  });

} // namespace
