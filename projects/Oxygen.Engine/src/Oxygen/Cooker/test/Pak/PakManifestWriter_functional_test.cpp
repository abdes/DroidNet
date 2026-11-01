//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/PakManifestWriter.cpp

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakBuildResult.h>
#include <Oxygen/Cooker/Pak/PakManifestWriter.h>
#include <Oxygen/Cooker/Pak/PakPlan.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/PakWorkspace.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace data = oxygen::data;
namespace fixtures = oxygen::cooker::testing;
namespace pak = oxygen::content::pak;

using oxygen::cooker::test::HasDiagnostic;
using oxygen::cooker::test::HasNoErrors;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::PakWorkspace;

constexpr auto kContentVersion = uint16_t { 7U };
constexpr auto kPatchDiffBasisIdentifier
  = std::string_view { "descriptor_plus_transitive_resources_v1" };

//! The base of the `Game` library holding a material and a geometry.
auto TwoAssetBase() -> data::PakCatalog
{
  const auto game = LooseCookedRoot {};
  return game.BaseCatalog({
    {
      .asset_key = game.KeyOf("Wood.omat"),
      .asset_type = data::AssetType::kMaterial,
    },
    {
      .asset_key = game.KeyOf("Mesh.ogeo"),
      .asset_type = data::AssetType::kGeometry,
    },
  });
}

//! A full build of the `Game` library holding one material, emitting its
//! manifest in full.
class PakManifestTest : public ::testing::Test {
protected:
  static constexpr auto kMaterialLeaf = std::string_view { "Wood.omat" };

  //! The library's one material.
  [[nodiscard]] static auto Material() -> data::AssetKey
  {
    return LooseCookedRoot {}.KeyOf(kMaterialLeaf);
  }

  //! The full build of the library that emits `full.manifest.json`.
  auto FullRequest() -> pak::PakBuildRequest
  {
    auto game = LooseCookedRoot {};
    static_cast<void>(game.Add({
      .type = data::AssetType::kMaterial,
      .leaf = std::string(kMaterialLeaf),
      .descriptor = fixtures::MaterialBuilder { "Wood" }.Bytes(),
    }));
    auto request = workspace_.FullRequest({ workspace_.Add(game) }, "full.pak");
    request.output_manifest_path = PakWorkspace::PakPath("full.manifest.json");
    request.content_version = kContentVersion;
    request.options.emit_manifest_in_full = true;
    return request;
  }

  PakWorkspace workspace_;
};

//=== Patch manifests ===-----------------------------------------------------//

//! A patch with no sources deletes every base asset, and its envelope
//! requires each base, in order.
NOLINT_TEST_F(
  PakManifestTest, PatchModeEmitsManifestWithDeletedSetAndCompatibilityEnvelope)
{
  // Arrange
  const auto base = TwoAssetBase();
  auto request = workspace_.PatchRequest({}, { base });
  request.content_version = kContentVersion;

  // Act
  const auto result = PakWorkspace::Build(request);

  // Assert
  EXPECT_THAT(result.diagnostics, HasNoErrors());
  ASSERT_HAS_VALUE(result.patch_manifest);
  ASSERT_TRUE(workspace_.Exists(request.output_manifest_path));
  ASSERT_HAS_VALUE(result.telemetry.manifest_duration);
  const auto& manifest = *result.patch_manifest;
  EXPECT_TRUE(manifest.created.empty());
  EXPECT_TRUE(manifest.replaced.empty());
  EXPECT_EQ(manifest.deleted.size(), base.entries.size());
  EXPECT_EQ(manifest.diff_basis_identifier, kPatchDiffBasisIdentifier);
  EXPECT_EQ(manifest.patch_source_key, request.source_key);
  EXPECT_EQ(
    manifest.compatibility_envelope.patch_content_version, kContentVersion);
  const auto& bases = manifest.compatibility_envelope.required_base_layers;
  ASSERT_EQ(bases.size(), 1U);
  EXPECT_EQ(bases.front().source_key, base.source_key);
  EXPECT_EQ(bases.front().content_version, base.content_version);
  EXPECT_EQ(bases.front().catalog_digest, base.catalog_digest);
  EXPECT_TRUE(manifest.patch_pak_digest.has_value());
  ASSERT_HAS_VALUE(manifest.patch_pak_crc32);
  EXPECT_EQ(*manifest.patch_pak_crc32, result.pak_crc32);
}

//! The writer checks the patch sets are disjoint before it emits anything.
NOLINT_TEST_F(
  PakManifestTest, ManifestValidationRejectsNonDisjointPatchSetsPreEmit)
{
  // Arrange
  const auto base = LooseCookedRoot {}.BaseCatalog();
  const auto request = workspace_.PatchRequest({}, { base });
  auto plan = pak::PakPlan::Data {};
  plan.catalog.bytes = base.Encode().value();
  plan.patch_actions = {
    {
      .asset_key = Material(),
      .asset_type = data::AssetType::kMaterial,
      .action = pak::PakPatchAction::kCreate,
    },
    {
      .asset_key = Material(),
      .asset_type = data::AssetType::kMaterial,
      .action = pak::PakPatchAction::kDelete,
    },
  };

  // Act
  const auto written = pak::PakManifestWriter {}.Write(
    request, pak::PakPlan(std::move(plan)), uint32_t { 0U });

  // Assert
  EXPECT_THAT(
    written.diagnostics, HasDiagnostic("pak.manifest.patch_sets_not_disjoint"));
  EXPECT_FALSE(written.manifest.has_value());
  EXPECT_FALSE(workspace_.Exists(request.output_manifest_path));
}

//=== Full manifests ===------------------------------------------------------//

//! A full build lists every asset it packs as created, and requires no base.
NOLINT_TEST_F(
  PakManifestTest, FullModeEmitManifestInFullWritesManifestWithFullSemantics)
{
  // Arrange
  auto request = FullRequest();
  request.options.compute_crc32 = false;

  // Act
  const auto result = PakWorkspace::Build(request);

  // Assert
  EXPECT_THAT(result.diagnostics, HasNoErrors());
  ASSERT_HAS_VALUE(result.patch_manifest);
  ASSERT_TRUE(workspace_.Exists(request.output_manifest_path));
  ASSERT_HAS_VALUE(result.telemetry.manifest_duration);
  const auto& manifest = *result.patch_manifest;
  EXPECT_THAT(manifest.created, ::testing::ElementsAre(Material()));
  EXPECT_TRUE(manifest.replaced.empty());
  EXPECT_TRUE(manifest.deleted.empty());
  EXPECT_TRUE(manifest.compatibility_envelope.required_base_layers.empty());
  EXPECT_EQ(
    manifest.compatibility_envelope.patch_content_version, kContentVersion);
  EXPECT_EQ(manifest.diff_basis_identifier, kPatchDiffBasisIdentifier);
  EXPECT_EQ(manifest.patch_source_key, request.source_key);
  EXPECT_TRUE(manifest.patch_pak_digest.has_value());
  EXPECT_TRUE(manifest.patch_pak_crc32.has_value());
  const auto text = workspace_.Text(request.output_manifest_path);
  EXPECT_THAT(text,
    ::testing::HasSubstr(
      "\n  \"created\": [\n    \"" + data::to_string(Material()) + "\"\n  ]"));
  EXPECT_THAT(text, ::testing::HasSubstr("\n  \"compatibility_envelope\": {"));
}

NOLINT_TEST_F(PakManifestTest, FullModeManifestEmissionIsBitExactDeterministic)
{
  // Arrange
  const auto request = FullRequest();
  ASSERT_THAT(PakWorkspace::Build(request).diagnostics, HasNoErrors());
  const auto first = workspace_.Text(request.output_manifest_path);
  ASSERT_FALSE(first.empty());

  // Act
  const auto rebuilt = PakWorkspace::Build(request);

  // Assert
  ASSERT_THAT(rebuilt.diagnostics, HasNoErrors());
  EXPECT_EQ(workspace_.Text(request.output_manifest_path), first);
}

//=== Manifests that cannot be written ===------------------------------------//

//! Builds whose manifest destination refuses to be published.
class PakManifestUnwritableTest : public ::testing::Test {
protected:
  //! `request`, its manifest destination refusing publication.
  [[nodiscard]] auto Unwritable(pak::PakBuildRequest request)
    -> pak::PakBuildRequest
  {
    workspace_.Storage().FailPublishing(request.output_manifest_path);
    return request;
  }

  //! The pak of the `Game` library holding one material.
  auto GamePak() -> std::filesystem::path
  {
    auto game = LooseCookedRoot {};
    static_cast<void>(game.Add({
      .type = data::AssetType::kMaterial,
      .leaf = "Wood.omat",
      .descriptor = fixtures::MaterialBuilder { "Wood" }.Bytes(),
    }));
    const auto request
      = workspace_.FullRequest({ workspace_.Add(game) }, "game.pak");
    static_cast<void>(workspace_.PackOrThrow(request));
    return request.output_pak_path;
  }

  PakWorkspace workspace_;
};

NOLINT_TEST_F(
  PakManifestUnwritableTest, PatchModeFailsWhenManifestCannotBeWritten)
{
  // Arrange
  const auto request
    = Unwritable(workspace_.PatchRequest({}, { TwoAssetBase() }));

  // Act
  const auto result = PakWorkspace::Build(request);

  // Assert
  EXPECT_THAT(
    result.diagnostics, HasDiagnostic("pak.manifest.write_open_failed"));
  EXPECT_FALSE(result.patch_manifest.has_value());
  EXPECT_FALSE(workspace_.Exists(request.output_manifest_path));
}

NOLINT_TEST_F(
  PakManifestUnwritableTest, FullModeFailsWhenManifestCannotBeWritten)
{
  // Arrange
  auto request = workspace_.FullRequest({ PakWorkspace::PakSource(GamePak()) });
  request.output_manifest_path = PakWorkspace::PakPath("out.manifest.json");
  request.options.emit_manifest_in_full = true;
  request = Unwritable(std::move(request));

  // Act
  const auto result = PakWorkspace::Build(request);

  // Assert
  EXPECT_THAT(
    result.diagnostics, HasDiagnostic("pak.manifest.write_open_failed"));
  EXPECT_FALSE(result.patch_manifest.has_value());
  EXPECT_FALSE(workspace_.Exists(request.output_manifest_path));
}

} // namespace
