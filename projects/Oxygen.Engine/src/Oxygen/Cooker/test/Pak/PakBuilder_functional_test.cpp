//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/PakBuilder.cpp

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/DependencyRequirementError.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakBuildResult.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/testing/Descriptors.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/PakWorkspace.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace oxygen::engine::internal {
//! The tag an `AssetLoader` takes, which only the engine and its tests make.
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
} // namespace oxygen::engine::internal

namespace {
namespace data = oxygen::data;
namespace fixtures = oxygen::cooker::testing;
namespace pak = oxygen::content::pak;
namespace render = oxygen::data::pak::render;
namespace script = oxygen::data::pak::scripting;

using oxygen::cooker::test::HasDiagnostic;
using oxygen::cooker::test::HasNoErrors;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::Materialize;
using oxygen::cooker::testing::PakWorkspace;
using oxygen::engine::internal::EngineTagFactory;
using oxygen::serio::testing::BytesEqual;

//=== Helpers ===-------------------------------------------------------------//

//! The bytes of a plain material named `name`.
auto MaterialBytes(const std::string_view name) -> std::vector<std::byte>
{
  return fixtures::MaterialDescriptor(name).bytes;
}

//! The bytes of a script with no bytecode.
auto ScriptBytes() -> std::vector<std::byte>
{
  auto desc = script::ScriptAssetDesc {};
  desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kScript);
  desc.header.version = script::kScriptAssetVersion;
  return data::MakeDescriptorBytes(desc);
}

//! The references of an asset that names the asset `target` of type `type`.
auto ReferencesTo(const data::AssetKey& target, const data::AssetType type)
  -> data::AssetReferences
{
  return data::AssetReferences::Create({},
    {
      {
        .key = target,
        .kind = data::KeyReferenceKind::kAsset,
        .expected_type = type,
      },
    })
    .value();
}

//! Adds a plain material at `leaf`; returns its key.
auto AddMaterial(LooseCookedRoot& library, const std::string_view leaf)
  -> data::AssetKey
{
  return library.Add({
    .type = data::AssetType::kMaterial,
    .leaf = std::string(leaf),
    .descriptor = MaterialBytes(leaf),
  });
}

//=== Reference graphs ===----------------------------------------------------//

//! What the owner material's reference to `Target.omat` finds.
enum class Target : uint8_t {
  kAbsent,
  kScript,
  kMaterialReferencingOwner,
};

//! One invalid reference graph and the diagnostic the planner must report.
struct ReferenceGraphCase final {
  const char* name;
  Target target;
  const char* code;
};

class PakBuilderReferenceGraphTest
  : public ::testing::TestWithParam<ReferenceGraphCase> { };

NOLINT_TEST_P(PakBuilderReferenceGraphTest, RejectsInvalidAssetReferenceGraph)
{
  // Arrange: forged metadata; production writers reject these graphs.
  auto workspace = PakWorkspace {};
  auto game = LooseCookedRoot {};
  const auto owner = game.KeyOf("Owner.omat");
  const auto target = game.KeyOf("Target.omat");
  game.Add({
    .type = data::AssetType::kMaterial,
    .leaf = "Owner.omat",
    .descriptor = MaterialBytes("Owner"),
    .references = ReferencesTo(target, data::AssetType::kMaterial),
  });
  if (GetParam().target == Target::kScript) {
    game.Add({
      .type = data::AssetType::kScript,
      .leaf = "Target.oscript",
      .descriptor = ScriptBytes(),
      .key = target,
    });
  } else if (GetParam().target == Target::kMaterialReferencingOwner) {
    game.Add({
      .type = data::AssetType::kMaterial,
      .leaf = "Target.omat",
      .descriptor = MaterialBytes("Target"),
      .references = ReferencesTo(owner, data::AssetType::kMaterial),
    });
  }
  const auto request = workspace.FullRequest({ workspace.Add(game) });

  // Act
  const auto result = PakWorkspace::Build(request);

  // Assert
  EXPECT_THAT(result.diagnostics, HasDiagnostic(GetParam().code));
  EXPECT_FALSE(workspace.Exists(request.output_pak_path));
}

INSTANTIATE_TEST_SUITE_P(InvalidGraphs, PakBuilderReferenceGraphTest,
  ::testing::Values(ReferenceGraphCase { "MissingTarget", Target::kAbsent,
                      "pak.plan.asset_reference_missing" },
    ReferenceGraphCase { "TypeMismatch", Target::kScript,
      "pak.plan.asset_reference_type_mismatch" },
    ReferenceGraphCase { "Cycle", Target::kMaterialReferencingOwner,
      "pak.plan.asset_reference_cycle" }),
  [](const auto& info) { return std::string(info.param.name); });

//=== Sentinel texture references ===-----------------------------------------//

//! The error-texture and no-texture sentinels a material references survive a
//! pack from loose content and a repack from that pak.
NOLINT_TEST(PakBuilderTest, SentinelTextureReferencesSurvivePackAndRepack)
{
  // Arrange
  auto workspace = PakWorkspace {};
  auto game = LooseCookedRoot {};
  const auto references = data::AssetReferences::Create(
    {
      {
        .kind = data::ResourceKind::kTexture,
        .index = data::pak::core::kErrorTextureResourceIndex,
      },
      {
        .kind = data::ResourceKind::kTexture,
        .index = data::pak::core::kNoResourceIndex,
      },
    },
    {});
  ASSERT_HAS_VALUE(references);
  auto desc = render::MaterialAssetDesc {};
  desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kMaterial);
  desc.header.version = render::kMaterialAssetVersion;
  desc.base_color_texture = data::ResourceReferenceIndex { 0U };
  desc.emissive_texture = data::ResourceReferenceIndex { 1U };
  const auto descriptor = data::MakeDescriptorBytes(desc);
  const auto material = game.Add({
    .type = data::AssetType::kMaterial,
    .leaf = "Missing.omat",
    .descriptor = descriptor,
    .references = *references,
  });
  const auto packed
    = workspace.FullRequest({ workspace.Add(game) }, "packed.pak");
  const auto repacked = workspace.FullRequest(
    { PakWorkspace::PakSource(packed.output_pak_path) }, "repacked.pak");

  for (const auto& request : { packed, repacked }) {
    SCOPED_TRACE(request.output_pak_path.generic_string());

    // Act
    const auto result = PakWorkspace::Build(request);

    // Assert
    ASSERT_THAT(result.diagnostics, HasNoErrors());
    const auto archive = workspace.Open(request.output_pak_path);
    EXPECT_EQ(archive.Pak().ReadAssetReferences(material), *references);
    EXPECT_THAT(archive.Descriptor(material), BytesEqual(descriptor));
  }
}

//=== Dependencies ===--------------------------------------------------------//

//! A full build of the `Game` layer above the `Engine` library: the layer's
//! owner material references the library's target material with an expected
//! type. Derives from `TempDirTest` for the `AssetLoader`, which mounts real
//! files only.
class PakBuilderDependencyTest : public oxygen::testing::TempDirTest {
protected:
  //! Packs `engine.pak`, then builds `layer.pak` and its manifest above it.
  auto BuildLayer(const data::AssetType expected_type) -> pak::PakBuildResult
  {
    auto engine = LooseCookedRoot { "Engine" };
    target_ = AddMaterial(engine, "Target.omat");
    const auto engine_pak = workspace_.PackOrThrow(
      workspace_.FullRequest({ workspace_.Add(engine) }, "engine.pak"));

    auto game = LooseCookedRoot { "Game" };
    owner_ = game.Add({
      .type = data::AssetType::kMaterial,
      .leaf = "Owner.omat",
      .descriptor = MaterialBytes("Owner"),
      .references = ReferencesTo(target_, expected_type),
    });
    auto request
      = workspace_.FullRequest({ workspace_.Add(game) }, "layer.pak");
    request.output_manifest_path = PakWorkspace::PakPath("layer.manifest.json");
    request.options.emit_manifest_in_full = true;
    request.dependencies = {
      { .catalog = engine_pak.Pak().Catalog(), .texture_keys = {} },
    };
    return PakWorkspace::Build(request);
  }

  //! The requirement the layer states of the `Engine` mount.
  [[nodiscard]] auto Required() const -> data::PakCatalogDependency
  {
    return {
      .mount = "Engine",
      .requirements = { {
        .kind = data::KeyReferenceKind::kAsset,
        .key = target_,
        .type = data::AssetType::kMaterial,
      } },
    };
  }

  PakWorkspace workspace_;
  data::AssetKey owner_;
  data::AssetKey target_;
};

//! References into a dependency resolve without copying its asset, and the
//! catalog and manifest require the asset of the dependency's library.
NOLINT_TEST_F(PakBuilderDependencyTest, ReferencesResolveIntoTheDependency)
{
  // Act
  const auto layer = BuildLayer(data::AssetType::kMaterial);

  // Assert
  EXPECT_THAT(layer.diagnostics, HasNoErrors());
  const auto packed = workspace_.Open(PakWorkspace::PakPath("layer.pak"));
  const auto& archive = packed.Pak();
  EXPECT_TRUE(archive.FindEntry(owner_).has_value());
  EXPECT_FALSE(archive.FindEntry(target_).has_value());
  EXPECT_EQ(archive.Catalog().mount, "Game");
  EXPECT_TRUE(archive.Catalog().bases.empty());
  EXPECT_THAT(
    archive.Catalog().dependencies, ::testing::ElementsAre(Required()));
  ASSERT_HAS_VALUE(layer.patch_manifest);
  const auto& envelope = layer.patch_manifest->compatibility_envelope;
  EXPECT_TRUE(envelope.required_base_layers.empty());
  EXPECT_THAT(
    envelope.required_dependencies, ::testing::ElementsAre(Required()));
}

//! The requirements the planner writes are the ones the runtime checks: the
//! layer mounts above its library and is refused without it.
NOLINT_TEST_F(PakBuilderDependencyTest, LayerMountsOnlyAboveItsLibrary)
{
  // Arrange
  const auto layer = BuildLayer(data::AssetType::kMaterial);
  ASSERT_THAT(layer.diagnostics, HasNoErrors());
  const auto engine_pak = TempPath("engine.pak");
  const auto layer_pak = TempPath("layer.pak");
  Materialize(workspace_.Storage().Bytes(PakWorkspace::PakPath("engine.pak")),
    engine_pak);
  Materialize(
    workspace_.Storage().Bytes(PakWorkspace::PakPath("layer.pak")), layer_pak);
  auto with_library = oxygen::content::AssetLoader(EngineTagFactory::Get());
  auto without_library = oxygen::content::AssetLoader(EngineTagFactory::Get());
  with_library.AddPakFile(engine_pak);

  // Act and Assert
  NOLINT_EXPECT_NO_THROW(with_library.AddPakFile(layer_pak));
  NOLINT_EXPECT_THROW(without_library.AddPakFile(layer_pak),
    oxygen::content::DependencyRequirementError);
}

//! A reference into a dependency still checks the referenced asset's type.
NOLINT_TEST_F(PakBuilderDependencyTest, ReferenceTypesAreChecked)
{
  // Act
  const auto layer = BuildLayer(data::AssetType::kScript);

  // Assert
  EXPECT_THAT(
    layer.diagnostics, HasDiagnostic("pak.plan.asset_reference_type_mismatch"));
  EXPECT_FALSE(workspace_.Exists(PakWorkspace::PakPath("layer.pak")));
}

//=== Textures named by key ===-----------------------------------------------//

//! A material of library `Game` that names a texture by key, built beside a
//! source of its own library holding that texture, on top of a dependency
//! holding it, or with nothing holding it.
class PakBuilderExternalTextureTest : public ::testing::Test {
protected:
  static constexpr auto kGrid = std::string_view { "Textures/Grid.otex" };

  //! Builds `layer.pak`: `sources`, then the owner material naming the
  //! texture at `texture`, above `dependencies`.
  auto BuildLayer(const std::string_view texture,
    std::vector<data::CookedSource> sources,
    std::vector<pak::PakDependency> dependencies = {}) -> pak::PakBuildResult
  {
    auto desc = render::MaterialAssetDesc {};
    desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kMaterial);
    desc.header.version = render::kMaterialAssetVersion;
    desc.base_color_texture = data::ResourceReferenceIndex { 0U };
    auto game = LooseCookedRoot {};
    owner_ = game.Add({
      .type = data::AssetType::kMaterial,
      .leaf = "Owner.omat",
      .descriptor = data::MakeDescriptorBytes(desc),
      .references = data::AssetReferences::Create(
        {
          {
            .kind = data::ResourceKind::kExternalTexture,
            .index = data::pak::core::ResourceIndexT { 0U },
          },
        },
        {
          {
            .key = data::TextureKeyFromVirtualPath(texture),
            .kind = data::KeyReferenceKind::kTexture,
          },
        })
        .value(),
    });
    sources.push_back(workspace_.Add(game));
    auto request = workspace_.FullRequest(std::move(sources), "layer.pak");
    request.dependencies = std::move(dependencies);
    return PakWorkspace::Build(request);
  }

  //! Tells the texture library of `Game` from the owner's.
  static constexpr auto kTextureLibrarySeed = uint8_t { 2U };

  PakWorkspace workspace_;
  data::AssetKey owner_;
};

//! A texture a source of the pak holds is packed with the material.
NOLINT_TEST_F(PakBuilderExternalTextureTest, OwnTextureIsPacked)
{
  // Arrange
  auto textures = LooseCookedRoot { "Game", kTextureLibrarySeed };
  const auto grid = textures.DescribeTexture(
    kGrid, textures.AddTexture(oxygen::cooker::testing::SeedBytes(1U)));

  // Act
  const auto layer = BuildLayer(grid, { workspace_.Add(textures) });

  // Assert
  EXPECT_THAT(layer.diagnostics, HasNoErrors());
  const auto packed = workspace_.Open(PakWorkspace::PakPath("layer.pak"));
  const auto& archive = packed.Pak();
  EXPECT_TRUE(archive.ResolveTextureIndexByVirtualPath(grid).has_value());
  EXPECT_TRUE(archive.Catalog().dependencies.empty());
}

//! A texture a dependency holds is left to it and required of its library.
NOLINT_TEST_F(PakBuilderExternalTextureTest, DependencyTextureIsRequired)
{
  // Arrange
  auto engine = LooseCookedRoot { "Engine" };
  const auto grid = engine.DescribeTexture(
    kGrid, engine.AddTexture(oxygen::cooker::testing::SeedBytes(1U)));
  const auto engine_pak = workspace_.PackOrThrow(
    workspace_.FullRequest({ workspace_.Add(engine) }, "engine.pak"));
  auto dependency = pak::PakDependency {
    .catalog = engine_pak.Pak().Catalog(),
    .texture_keys = { data::TextureKeyFromVirtualPath(grid) },
  };

  // Act
  const auto layer = BuildLayer(grid, {}, { std::move(dependency) });

  // Assert
  EXPECT_THAT(layer.diagnostics, HasNoErrors());
  const auto packed = workspace_.Open(PakWorkspace::PakPath("layer.pak"));
  const auto& archive = packed.Pak();
  EXPECT_FALSE(archive.ResolveTextureIndexByVirtualPath(grid).has_value());
  EXPECT_THAT(archive.Catalog().dependencies,
    ::testing::ElementsAre(data::PakCatalogDependency {
      .mount = "Engine",
      .requirements = { {
        .kind = data::KeyReferenceKind::kTexture,
        .key = data::TextureKeyFromVirtualPath(grid),
        .type = data::AssetType::kUnknown,
      } },
    }));
}

//! A texture neither the pak nor its lower layers hold refuses the build.
NOLINT_TEST_F(PakBuilderExternalTextureTest, UnheldTextureIsRefused)
{
  // Act
  const auto layer
    = BuildLayer(LooseCookedRoot { "Engine" }.VirtualPath(kGrid), {});

  // Assert
  EXPECT_THAT(
    layer.diagnostics, HasDiagnostic("pak.plan.texture_reference_missing"));
  EXPECT_FALSE(workspace_.Exists(PakWorkspace::PakPath("layer.pak")));
}

//=== Request validation ===--------------------------------------------------//

//! Bases are the exact stack a patch applies to; a full build is rejected.
NOLINT_TEST(PakBuilderTest, FullBuildRejectsBaseCatalogs)
{
  // Arrange
  auto workspace = PakWorkspace {};
  auto request = workspace.FullRequest({}, "full.pak");
  request.base_catalogs = { LooseCookedRoot {}.BaseCatalog() };

  // Act
  const auto result = PakWorkspace::Build(request);

  // Assert
  EXPECT_THAT(result.diagnostics,
    HasDiagnostic("pak.request.full_forbids_base_catalogs"));
  EXPECT_FALSE(workspace.Exists(request.output_pak_path));
}

//! A patch with no identity, no bases and no manifest path reports all three
//! errors at once.
NOLINT_TEST(PakBuilderTest, PatchRequestValidationEmitsAllModeErrors)
{
  // Arrange
  auto workspace = PakWorkspace {};
  auto request = workspace.PatchRequest({}, {});
  request.output_manifest_path.clear();
  request.content_version = 0U;
  request.source_key = {};

  // Act
  const auto result = PakWorkspace::Build(request);

  // Assert
  EXPECT_EQ(result.summary.diagnostics_error, 3U);
  EXPECT_THAT(result.diagnostics, HasDiagnostic("pak.request.source_key_zero"));
  EXPECT_THAT(result.diagnostics,
    HasDiagnostic("pak.request.patch_requires_base_catalogs"));
  EXPECT_THAT(result.diagnostics,
    HasDiagnostic("pak.request.patch_requires_output_manifest_path"));
}

NOLINT_TEST(PakBuilderTest, FullManifestOptionRequiresOutputManifestPath)
{
  // Arrange
  auto workspace = PakWorkspace {};
  auto request = workspace.FullRequest({});
  request.options.emit_manifest_in_full = true;

  // Act
  const auto result = PakWorkspace::Build(request);

  // Assert
  EXPECT_EQ(result.summary.diagnostics_error, 1U);
  EXPECT_THAT(result.diagnostics,
    HasDiagnostic("pak.request.full_manifest_requires_output_manifest_path"));
}

//=== Builds ===--------------------------------------------------------------//

//! A full build of the `Game` library holding one plain material.
class PakBuilderBuildTest : public ::testing::Test {
protected:
  static constexpr auto kMaterialLeaf
    = std::string_view { "Materials/Main.omat" };

  //! The library's one material.
  [[nodiscard]] static auto Material() -> data::AssetKey
  {
    return LooseCookedRoot {}.KeyOf(kMaterialLeaf);
  }

  //! Its descriptor's digest.
  [[nodiscard]] static auto MaterialDigest() -> oxygen::base::Sha256Digest
  {
    return oxygen::base::ComputeSha256(MaterialBytes(kMaterialLeaf));
  }

  //! The library as a source of the workspace.
  auto Source() -> data::CookedSource
  {
    auto game = LooseCookedRoot {};
    static_cast<void>(AddMaterial(game, kMaterialLeaf));
    return workspace_.Add(game);
  }

  PakWorkspace workspace_;
};

NOLINT_TEST_F(PakBuilderBuildTest, ValidRequestWritesPakAndReportsTelemetry)
{
  // Arrange
  const auto request = workspace_.FullRequest({ Source() });

  // Act
  const auto result = PakWorkspace::Build(request);

  // Assert
  EXPECT_THAT(result.diagnostics, HasNoErrors());
  EXPECT_THAT(result.diagnostics,
    ::testing::Not(HasDiagnostic("pak.write.phase3_writer_unavailable")));
  EXPECT_GT(result.file_size, 0U);
  EXPECT_NE(result.pak_crc32, 0U);
  EXPECT_EQ(result.output_catalog.source_key, request.source_key);
  EXPECT_EQ(result.output_catalog.content_version, request.content_version);
  ASSERT_EQ(result.output_catalog.entries.size(), 1U);
  EXPECT_EQ(result.output_catalog.entries.front().asset_key, Material());
  EXPECT_NE(
    result.output_catalog.catalog_digest, data::PakCatalog {}.catalog_digest);
  EXPECT_FALSE(result.patch_manifest.has_value());
  EXPECT_TRUE(result.telemetry.total_duration.has_value());
  EXPECT_TRUE(result.telemetry.planning_duration.has_value());
  EXPECT_TRUE(result.telemetry.writing_duration.has_value());
  EXPECT_FALSE(result.telemetry.manifest_duration.has_value());
}

NOLINT_TEST_F(
  PakBuilderBuildTest, FullBuildWithManifestEmitsPatchManifestAndTelemetry)
{
  // Arrange
  auto request = workspace_.FullRequest({ Source() });
  request.output_manifest_path = PakWorkspace::PakPath("out.manifest.json");
  request.options.emit_manifest_in_full = true;

  // Act
  const auto result = PakWorkspace::Build(request);

  // Assert
  EXPECT_THAT(result.diagnostics, HasNoErrors());
  EXPECT_TRUE(result.patch_manifest.has_value());
  EXPECT_TRUE(result.telemetry.manifest_duration.has_value());
}

NOLINT_TEST_F(
  PakBuilderBuildTest, FullBuildReturnsOutputCatalogEntriesForEmittedAssets)
{
  // Arrange
  constexpr auto kContentVersion = uint16_t { 7U };
  auto request = workspace_.FullRequest({ Source() });
  request.content_version = kContentVersion;

  // Act
  const auto result = PakWorkspace::Build(request);

  // Assert
  EXPECT_THAT(result.diagnostics, HasNoErrors());
  EXPECT_EQ(result.output_catalog.source_key, request.source_key);
  EXPECT_EQ(result.output_catalog.content_version, kContentVersion);
  ASSERT_EQ(result.output_catalog.entries.size(), 1U);
  const auto& entry = result.output_catalog.entries.front();
  EXPECT_EQ(entry.asset_key, Material());
  EXPECT_EQ(entry.asset_type, data::AssetType::kMaterial);
  EXPECT_EQ(entry.descriptor_digest, MaterialDigest());
  // A material binding no resource digests to its descriptor alone.
  EXPECT_EQ(entry.transitive_resource_digest, MaterialDigest());
  EXPECT_NE(
    result.output_catalog.catalog_digest, data::PakCatalog {}.catalog_digest);
}

NOLINT_TEST_F(PakBuilderBuildTest,
  PatchBuildReturnsOutputCatalogEntriesForEmittedPatchAssets)
{
  // Arrange
  constexpr auto kContentVersion = uint16_t { 5U };
  const auto stale = oxygen::base::ComputeSha256(MaterialBytes("Stale"));
  auto request = workspace_.PatchRequest({ Source() },
    { LooseCookedRoot {}.BaseCatalog({
      {
        .asset_key = Material(),
        .asset_type = data::AssetType::kMaterial,
        .descriptor_digest = stale,
        .transitive_resource_digest = stale,
      },
    }) });
  request.content_version = kContentVersion;

  // Act
  const auto result = PakWorkspace::Build(request);

  // Assert
  EXPECT_THAT(result.diagnostics, HasNoErrors());
  EXPECT_EQ(result.summary.patch_replaced, 1U);
  EXPECT_EQ(result.output_catalog.source_key, request.source_key);
  EXPECT_EQ(result.output_catalog.content_version, kContentVersion);
  ASSERT_EQ(result.output_catalog.entries.size(), 1U);
  const auto& entry = result.output_catalog.entries.front();
  EXPECT_EQ(entry.asset_key, Material());
  EXPECT_EQ(entry.asset_type, data::AssetType::kMaterial);
  EXPECT_EQ(entry.descriptor_digest, MaterialDigest());
  EXPECT_EQ(entry.transitive_resource_digest, MaterialDigest());
  EXPECT_NE(
    result.output_catalog.catalog_digest, data::PakCatalog {}.catalog_digest);
  EXPECT_TRUE(result.patch_manifest.has_value());
  EXPECT_TRUE(result.telemetry.manifest_duration.has_value());
}

//! A pak the current builder wrote raises no warning when repacked, so a
//! strict policy does not fail the build.
NOLINT_TEST_F(
  PakBuilderBuildTest, CurrentPakInputBuildsWithoutWarningsUnderStrictPolicy)
{
  // Arrange
  const auto base = workspace_.FullRequest({ Source() }, "base.pak");
  ASSERT_THAT(PakWorkspace::Build(base).diagnostics, HasNoErrors());
  auto request = workspace_.FullRequest(
    { PakWorkspace::PakSource(base.output_pak_path) }, "strict.pak");
  request.options.fail_on_warnings = true;

  // Act
  const auto result = PakWorkspace::Build(request);

  // Assert
  EXPECT_EQ(result.summary.diagnostics_warning, 0U);
  EXPECT_THAT(result.diagnostics, HasNoErrors());
  EXPECT_THAT(result.diagnostics,
    ::testing::Not(HasDiagnostic("pak.request.fail_on_warnings")));
  EXPECT_TRUE(result.telemetry.planning_duration.has_value());
  EXPECT_TRUE(result.telemetry.writing_duration.has_value());
}

//=== Patches over base catalogs ===------------------------------------------//

//! Two bases of one library may not disagree on an asset's type.
NOLINT_TEST(PakBuilderTest, PlannerRejectsBaseCatalogTypeMismatch)
{
  // Arrange
  auto workspace = PakWorkspace {};
  const auto game = LooseCookedRoot {};
  const auto asset = game.KeyOf("Asset");
  const auto request = workspace.PatchRequest({},
    {
      game.BaseCatalog(
        { { .asset_key = asset, .asset_type = data::AssetType::kGeometry } },
        0U),
      game.BaseCatalog(
        { { .asset_key = asset, .asset_type = data::AssetType::kMaterial } },
        1U),
    });

  // Act
  const auto result = PakWorkspace::Build(request);

  // Assert
  EXPECT_THAT(
    result.diagnostics, HasDiagnostic("pak.plan.base_catalog_type_mismatch"));
}

//! A patch with no sources deletes every asset of its base.
NOLINT_TEST(PakBuilderTest, PatchModeClassifiesMissingSourceAsDelete)
{
  // Arrange
  auto workspace = PakWorkspace {};
  const auto game = LooseCookedRoot {};
  const auto base = game.BaseCatalog({
    { .asset_key = game.KeyOf("Mesh.ogeo"),
      .asset_type = data::AssetType::kGeometry },
    { .asset_key = game.KeyOf("Wood.omat"),
      .asset_type = data::AssetType::kMaterial },
  });
  const auto request = workspace.PatchRequest({}, { base });

  // Act
  const auto result = PakWorkspace::Build(request);

  // Assert
  EXPECT_THAT(result.diagnostics, HasNoErrors());
  EXPECT_EQ(result.summary.patch_created, 0U);
  EXPECT_EQ(result.summary.patch_replaced, 0U);
  EXPECT_EQ(result.summary.patch_deleted, base.entries.size());
  EXPECT_EQ(result.summary.patch_unchanged, 0U);
}

} // namespace
