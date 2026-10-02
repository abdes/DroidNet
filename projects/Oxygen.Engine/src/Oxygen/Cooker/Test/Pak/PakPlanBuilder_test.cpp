//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <ios>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "PakTestSupport.h"

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Content/VirtualPathResolver.h>
#include <Oxygen/Cooker/Pak/PakBuildReport.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakPlan.h>
#include <Oxygen/Cooker/Pak/PakPlanBuilder.h>
#include <Oxygen/Cooker/Pak/PakWriter.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormatSerioLoaders.h>
#include <Oxygen/Data/PakFormatSerioWriters.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
} // namespace oxygen::engine::internal

using oxygen::base::CheckedAt;

namespace {
namespace data = oxygen::data;
namespace pak = oxygen::content::pak;
namespace paktest = oxygen::content::pak::test;
namespace lc = oxygen::data::loose_cooked;
namespace core = oxygen::data::pak::core;
namespace physics = oxygen::data::pak::physics;
namespace render = oxygen::data::pak::render;
namespace script = oxygen::data::pak::scripting;

using paktest::AssetSpec;
using paktest::FileSpec;

auto MakeNonZeroSourceKey(const uint8_t seed) -> data::SourceKey
{
  return paktest::MakeSourceKey(seed);
}

auto MakeAssetKey(const uint8_t seed) -> data::AssetKey
{
  return paktest::MakeAssetKey(seed);
}

auto HasError(std::span<const pak::PakDiagnostic> diagnostics) -> bool
{
  return paktest::HasError(diagnostics);
}

auto HasDiagnosticCode(std::span<const pak::PakDiagnostic> diagnostics,
  const std::string_view code) -> bool
{
  return paktest::HasDiagnosticCode(diagnostics, code);
}

auto FindTable(const pak::PakPlan& plan, const std::string_view name)
  -> const pak::PakTablePlan*
{
  const auto tables = plan.Tables();
  const auto it = std::ranges::find_if(tables,
    [name](const auto& table) -> auto { return table.table_name == name; });
  if (it == tables.end()) {
    return nullptr;
  }
  return std::addressof(*it);
}

auto ComputeExpectedBrowsePayloadSize(
  std::span<const pak::PakBrowseEntryPlan> entries) -> uint64_t
{
  uint64_t string_bytes = 0;
  for (const auto& entry : entries) {
    string_bytes += entry.virtual_path.size();
  }
  return static_cast<uint64_t>(sizeof(core::PakBrowseIndexHeader))
    + (static_cast<uint64_t>(entries.size())
      * sizeof(core::PakBrowseIndexEntry))
    + string_bytes;
}

auto ExpectPlansEquivalent(const pak::PakPlan& lhs, const pak::PakPlan& rhs)
  -> void
{
  EXPECT_EQ(lhs.Header().offset, rhs.Header().offset);
  EXPECT_EQ(lhs.Header().size_bytes, rhs.Header().size_bytes);
  EXPECT_EQ(lhs.Header().content_version, rhs.Header().content_version);
  EXPECT_EQ(lhs.Header().source_key.get(), rhs.Header().source_key.get());
  EXPECT_EQ(lhs.PlannedFileSize(), rhs.PlannedFileSize());

  const auto lhs_regions = lhs.Regions();
  const auto rhs_regions = rhs.Regions();
  ASSERT_EQ(lhs_regions.size(), rhs_regions.size());
  for (size_t i = 0; i < lhs_regions.size(); ++i) {
    EXPECT_EQ(CheckedAt(lhs_regions, i).region_name,
      CheckedAt(rhs_regions, i).region_name);
    EXPECT_EQ(
      CheckedAt(lhs_regions, i).offset, CheckedAt(rhs_regions, i).offset);
    EXPECT_EQ(CheckedAt(lhs_regions, i).size_bytes,
      CheckedAt(rhs_regions, i).size_bytes);
    EXPECT_EQ(
      CheckedAt(lhs_regions, i).alignment, CheckedAt(rhs_regions, i).alignment);
  }

  const auto lhs_tables = lhs.Tables();
  const auto rhs_tables = rhs.Tables();
  ASSERT_EQ(lhs_tables.size(), rhs_tables.size());
  for (size_t i = 0; i < lhs_tables.size(); ++i) {
    EXPECT_EQ(
      CheckedAt(lhs_tables, i).table_name, CheckedAt(rhs_tables, i).table_name);
    EXPECT_EQ(CheckedAt(lhs_tables, i).offset, CheckedAt(rhs_tables, i).offset);
    EXPECT_EQ(
      CheckedAt(lhs_tables, i).size_bytes, CheckedAt(rhs_tables, i).size_bytes);
    EXPECT_EQ(CheckedAt(lhs_tables, i).count, CheckedAt(rhs_tables, i).count);
    EXPECT_EQ(
      CheckedAt(lhs_tables, i).entry_size, CheckedAt(rhs_tables, i).entry_size);
    EXPECT_EQ(CheckedAt(lhs_tables, i).expected_entry_size,
      CheckedAt(rhs_tables, i).expected_entry_size);
    EXPECT_EQ(
      CheckedAt(lhs_tables, i).alignment, CheckedAt(rhs_tables, i).alignment);
    EXPECT_EQ(CheckedAt(lhs_tables, i).index_zero_required,
      CheckedAt(rhs_tables, i).index_zero_required);
    EXPECT_EQ(CheckedAt(lhs_tables, i).index_zero_present,
      CheckedAt(rhs_tables, i).index_zero_present);
    EXPECT_EQ(CheckedAt(lhs_tables, i).index_zero_forbidden,
      CheckedAt(rhs_tables, i).index_zero_forbidden);
  }

  const auto lhs_assets = lhs.Assets();
  const auto rhs_assets = rhs.Assets();
  ASSERT_EQ(lhs_assets.size(), rhs_assets.size());
  for (size_t i = 0; i < lhs_assets.size(); ++i) {
    EXPECT_EQ(
      CheckedAt(lhs_assets, i).asset_key, CheckedAt(rhs_assets, i).asset_key);
    EXPECT_EQ(
      CheckedAt(lhs_assets, i).asset_type, CheckedAt(rhs_assets, i).asset_type);
    EXPECT_EQ(CheckedAt(lhs_assets, i).offset, CheckedAt(rhs_assets, i).offset);
    EXPECT_EQ(
      CheckedAt(lhs_assets, i).size_bytes, CheckedAt(rhs_assets, i).size_bytes);
    EXPECT_EQ(
      CheckedAt(lhs_assets, i).alignment, CheckedAt(rhs_assets, i).alignment);
    EXPECT_EQ(CheckedAt(lhs_assets, i).reserved_bytes_zeroed,
      CheckedAt(rhs_assets, i).reserved_bytes_zeroed);
  }

  const auto lhs_resources = lhs.Resources();
  const auto rhs_resources = rhs.Resources();
  ASSERT_EQ(lhs_resources.size(), rhs_resources.size());
  for (size_t i = 0; i < lhs_resources.size(); ++i) {
    EXPECT_EQ(CheckedAt(lhs_resources, i).resource_kind,
      CheckedAt(rhs_resources, i).resource_kind);
    EXPECT_EQ(CheckedAt(lhs_resources, i).resource_index,
      CheckedAt(rhs_resources, i).resource_index);
    EXPECT_EQ(CheckedAt(lhs_resources, i).region_name,
      CheckedAt(rhs_resources, i).region_name);
    EXPECT_EQ(
      CheckedAt(lhs_resources, i).offset, CheckedAt(rhs_resources, i).offset);
    EXPECT_EQ(CheckedAt(lhs_resources, i).size_bytes,
      CheckedAt(rhs_resources, i).size_bytes);
    EXPECT_EQ(CheckedAt(lhs_resources, i).alignment,
      CheckedAt(rhs_resources, i).alignment);
    EXPECT_EQ(CheckedAt(lhs_resources, i).reserved_bytes_zeroed,
      CheckedAt(rhs_resources, i).reserved_bytes_zeroed);
  }

  EXPECT_EQ(lhs.Directory().offset, rhs.Directory().offset);
  EXPECT_EQ(lhs.Directory().size_bytes, rhs.Directory().size_bytes);
  ASSERT_EQ(lhs.Directory().entries.size(), rhs.Directory().entries.size());
  for (size_t i = 0; i < lhs.Directory().entries.size(); ++i) {
    const auto& lhs_entry = lhs.Directory().entries.at(i);
    const auto& rhs_entry = rhs.Directory().entries.at(i);
    EXPECT_EQ(lhs_entry.asset_key, rhs_entry.asset_key);
    EXPECT_EQ(lhs_entry.asset_type, rhs_entry.asset_type);
    EXPECT_EQ(lhs_entry.entry_offset, rhs_entry.entry_offset);
    EXPECT_EQ(lhs_entry.descriptor_offset, rhs_entry.descriptor_offset);
    EXPECT_EQ(lhs_entry.descriptor_size, rhs_entry.descriptor_size);
  }

  EXPECT_EQ(lhs.BrowseIndex().enabled, rhs.BrowseIndex().enabled);
  EXPECT_EQ(lhs.BrowseIndex().offset, rhs.BrowseIndex().offset);
  EXPECT_EQ(lhs.BrowseIndex().size_bytes, rhs.BrowseIndex().size_bytes);
  ASSERT_EQ(lhs.BrowseIndex().entries.size(), rhs.BrowseIndex().entries.size());
  for (size_t i = 0; i < lhs.BrowseIndex().entries.size(); ++i) {
    const auto& lhs_entry = lhs.BrowseIndex().entries.at(i);
    const auto& rhs_entry = rhs.BrowseIndex().entries.at(i);
    EXPECT_EQ(lhs_entry.asset_key, rhs_entry.asset_key);
    EXPECT_EQ(lhs_entry.virtual_path, rhs_entry.virtual_path);
  }

  EXPECT_EQ(lhs.Footer().offset, rhs.Footer().offset);
  EXPECT_EQ(lhs.Footer().size_bytes, rhs.Footer().size_bytes);
  EXPECT_EQ(lhs.Footer().crc32_field_absolute_offset,
    rhs.Footer().crc32_field_absolute_offset);
}

auto ExpectCatalogsEquivalent(
  const data::PakCatalog& lhs, const data::PakCatalog& rhs) -> void
{
  EXPECT_EQ(lhs.source_key, rhs.source_key);
  EXPECT_EQ(lhs.content_version, rhs.content_version);
  EXPECT_EQ(lhs.catalog_digest, rhs.catalog_digest);
  ASSERT_EQ(lhs.entries.size(), rhs.entries.size());
  for (size_t i = 0; i < lhs.entries.size(); ++i) {
    EXPECT_EQ(lhs.entries.at(i).asset_key, rhs.entries.at(i).asset_key);
    EXPECT_EQ(lhs.entries.at(i).asset_type, rhs.entries.at(i).asset_type);
    EXPECT_EQ(
      lhs.entries.at(i).descriptor_digest, rhs.entries.at(i).descriptor_digest);
    EXPECT_EQ(lhs.entries.at(i).transitive_resource_digest,
      rhs.entries.at(i).transitive_resource_digest);
  }
}

auto EnableDescriptorHash(std::vector<std::byte>& bytes) -> void
{
  const auto digest = oxygen::base::ComputeSha256(bytes);
  auto field = std::span(bytes).subspan(
    offsetof(core::AssetHeader, content_hash), digest.size());
  std::memcpy(field.data(), digest.data(), digest.size());
}

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

auto MakePhysicsSidecarSpec(const uint8_t seed, const AssetSpec& scene)
  -> AssetSpec
{
  auto descriptor = physics::PhysicsSceneAssetDesc {};
  descriptor.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kPhysicsScene);
  descriptor.header.version = physics::kPhysicsSceneAssetVersion;
  descriptor.target_scene_key = scene.key;
  const auto digest = oxygen::base::ComputeSha256(scene.descriptor_payload);
  std::ranges::copy(digest, std::begin(descriptor.target_scene_content_hash));
  const auto view = std::as_bytes(std::span { &descriptor, 1U });
  auto bytes = std::vector<std::byte>(view.begin(), view.end());
  EnableDescriptorHash(bytes);
  return AssetSpec {
    .key = MakeAssetKey(seed),
    .asset_type = data::AssetType::kPhysicsScene,
    .descriptor_relpath = "Physics.opscene",
    .virtual_path = "/Game/Physics" + std::to_string(seed) + ".opscene",
    .descriptor_size = bytes.size(),
    .descriptor_sha = oxygen::base::ComputeSha256(bytes),
    .descriptor_payload = std::move(bytes),
  };
}

auto CheckPackagedPhysics(oxygen::co::testing::TestEventLoop* loop,
  std::filesystem::path path, data::SourceKey source) -> oxygen::co::Co<>
{
  oxygen::co::ThreadPool pool(*loop, 2);
  oxygen::content::AssetLoaderConfig config {};
  config.thread_pool = oxygen::observer_ptr(&pool);
  oxygen::content::AssetLoader loader(
    oxygen::engine::internal::EngineTagFactory::Get(), config);
  OXCO_WITH_NURSERY(nursery)
  {
    co_await nursery.Start(
      &oxygen::content::AssetLoader::ActivateAsync, &loader);
    loader.Run();
    loader.AddPakFile(path);
    const auto key
      = loader.MakePhysicsResourceKey(source, core::ResourceIndexT { 1U });
    if (!key) {
      ADD_FAILURE() << "Packed physics payload has no locator";
      loader.Stop();
      co_return oxygen::co::kJoin;
    }
    const auto payload = co_await loader.LoadPhysicsResourceAsync(*key);
    if (!payload) {
      ADD_FAILURE() << "Packed physics payload did not load";
    } else {
      EXPECT_THAT(payload->GetData(), ::testing::ElementsAre(2U, 2U, 2U, 2U));
    }
    loader.Stop();
    co_return oxygen::co::kJoin;
  };
}

class PakPlanBuilderTest : public paktest::TempDirFixture { };

NOLINT_TEST_F(
  PakPlanBuilderTest, DeterministicPlanningPreservesDeclaredLayerPriority)
{
  using data::CookedSource;
  using data::CookedSourceKind;
  using pak::BuildMode;
  using pak::PakPlanBuilder;

  constexpr auto kAssetSeed = uint8_t { 0x21 };
  constexpr auto kDescriptorSize = uint64_t { 32U };
  constexpr auto kSourceKeySeed = uint8_t { 0x7A };
  constexpr auto kGuidA = uint8_t { 7U };
  constexpr auto kGuidZ = uint8_t { 19U };
  constexpr auto kShaA = uint8_t { 0x11U };
  constexpr auto kShaZ = uint8_t { 0x22U };

  const auto source_a = Root() / "a_source";
  const auto source_z = Root() / "z_source";

  auto asset_a = AssetSpec {
    .key = MakeAssetKey(kAssetSeed),
    .asset_type = data::AssetType::kMaterial,
    .descriptor_relpath = "A.desc",
    .virtual_path = "/Game/Asset.win",
    .descriptor_size = kDescriptorSize,
    .descriptor_sha = {},
  };
  asset_a.descriptor_sha.at(0) = kShaA;

  auto asset_z = AssetSpec {
    .key = MakeAssetKey(kAssetSeed),
    .asset_type = data::AssetType::kMaterial,
    .descriptor_relpath = "Z.desc",
    .virtual_path = "/Game/Asset.win",
    .descriptor_size = kDescriptorSize,
    .descriptor_sha = {},
  };
  asset_z.descriptor_sha.at(0) = kShaZ;

  ASSERT_TRUE(
    paktest::WriteLooseIndex(source_a, std::span<const AssetSpec>(&asset_a, 1U),
      std::span<const FileSpec> {}, kGuidA));
  ASSERT_TRUE(
    paktest::WriteLooseIndex(source_z, std::span<const AssetSpec>(&asset_z, 1U),
      std::span<const FileSpec> {}, kGuidZ));

  const auto options = pak::PakBuildOptions {
    .deterministic = true,
    .embed_browse_index = true,
    .emit_manifest_in_full = false,
    .compute_crc32 = true,
    .fail_on_warnings = false,
  };

  const auto request_a = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = {
      CookedSource { .kind = CookedSourceKind::kLooseCooked, .path = source_z },
      CookedSource { .kind = CookedSourceKind::kLooseCooked, .path = source_a },
    },
    .output_pak_path = Root() / "det_a.pak",
    .output_manifest_path = {},
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(kSourceKeySeed),
    .base_catalogs = {},

    .options = options,
  };

  const auto request_b = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = {
      CookedSource { .kind = CookedSourceKind::kLooseCooked, .path = source_a },
      CookedSource { .kind = CookedSourceKind::kLooseCooked, .path = source_z },
    },
    .output_pak_path = Root() / "det_b.pak",
    .output_manifest_path = {},
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(kSourceKeySeed),
    .base_catalogs = {},

    .options = options,
  };

  const auto builder = PakPlanBuilder {};
  const auto result_a = builder.Build(request_a);
  const auto result_b = builder.Build(request_b);

  ASSERT_FALSE(HasError(result_a.diagnostics));
  ASSERT_FALSE(HasError(result_b.diagnostics));
  if (!result_a.plan.has_value()) {
    FAIL() << "Expected result_a.plan to contain a value";
  }
  if (!result_b.plan.has_value()) {
    FAIL() << "Expected result_b.plan to contain a value";
  }

  const auto repeated = builder.Build(request_a);
  ASSERT_FALSE(HasError(repeated.diagnostics));
  ASSERT_TRUE(repeated.plan.has_value());
  ExpectPlansEquivalent(*result_a.plan, *repeated.plan);
  ExpectCatalogsEquivalent(result_a.output_catalog, repeated.output_catalog);

  const auto assets = result_a.plan->Assets();
  ASSERT_EQ(assets.size(), 1U);
  EXPECT_EQ(CheckedAt(assets, 0).asset_type, data::AssetType::kMaterial);
  ASSERT_EQ(result_a.output_catalog.entries.size(), 1U);
  EXPECT_EQ(
    result_a.output_catalog.entries.at(0).asset_key, MakeAssetKey(kAssetSeed));
  EXPECT_EQ(result_a.output_catalog.entries.at(0).asset_type,
    data::AssetType::kMaterial);
  EXPECT_EQ(result_a.output_catalog.entries.at(0).descriptor_digest,
    asset_a.descriptor_sha);
  ASSERT_EQ(result_b.output_catalog.entries.size(), 1U);
  EXPECT_EQ(result_b.output_catalog.entries.at(0).descriptor_digest,
    asset_z.descriptor_sha);

  // Same key, different type is invalid even when override order is explicit.
  asset_z.asset_type = data::AssetType::kScene;
  asset_z.descriptor_payload = paktest::MakeEmptySceneDescriptor();
  asset_z.descriptor_size = asset_z.descriptor_payload.size();
  ASSERT_TRUE(
    paktest::WriteLooseIndex(source_z, std::span<const AssetSpec>(&asset_z, 1U),
      std::span<const FileSpec> {}, kGuidZ));
  const auto incompatible = builder.Build(request_b);
  EXPECT_TRUE(HasDiagnosticCode(
    incompatible.diagnostics, "pak.plan.asset_override_type_mismatch"));
  EXPECT_FALSE(incompatible.plan.has_value());
}

NOLINT_TEST_F(
  PakPlanBuilderTest, IncrementalPatchRevertsReplacementAndPreservesDeletion)
{
  const auto root = Root() / "source";
  auto material = render::MaterialAssetDesc {};
  material.header.asset_type = static_cast<uint8_t>(data::AssetType::kMaterial);
  material.header.version = render::kMaterialAssetVersion;
  const auto material_bytes = [&] {
    const auto bytes = std::as_bytes(std::span { &material, 1U });
    return std::vector<std::byte>(bytes.begin(), bytes.end());
  };
  auto original_bytes = material_bytes();
  auto assets = std::array {
    AssetSpec { .key = MakeAssetKey(1U),
      .asset_type = data::AssetType::kMaterial,
      .descriptor_relpath = "M.omat",
      .virtual_path = "/Game/M.omat",
      .descriptor_size = original_bytes.size(),
      .descriptor_sha = oxygen::base::ComputeSha256(original_bytes),
      .descriptor_payload = original_bytes,
      .references = {} },
    AssetSpec { .key = MakeAssetKey(2U),
      .asset_type = data::AssetType::kMaterial,
      .descriptor_relpath = "D.omat",
      .virtual_path = "/Game/D.omat",
      .descriptor_size = original_bytes.size(),
      .descriptor_sha = oxygen::base::ComputeSha256(original_bytes),
      .descriptor_payload = original_bytes,
      .references = {} },
  };
  ASSERT_TRUE(paktest::WriteLooseIndex(root, assets, {}, 1U));
  auto request = pak::PakBuildRequest {
    .mode = pak::BuildMode::kFull,
    .sources
    = { { .kind = data::CookedSourceKind::kLooseCooked, .path = root } },
    .output_pak_path = Root() / "base.pak",
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(1U),
  };
  request.options.embed_browse_index = true;
  const auto build_and_write = [&] {
    auto result = pak::PakPlanBuilder {}.Build(request);
    for (const auto& diagnostic : result.diagnostics) {
      EXPECT_NE(diagnostic.severity, pak::PakDiagnosticSeverity::kError)
        << diagnostic.code << ": " << diagnostic.message;
    }
    if (result.plan) {
      EXPECT_FALSE(
        HasError(pak::PakWriter {}.Write(request, *result.plan).diagnostics));
    }
    return result;
  };
  const auto base = build_and_write();
  ASSERT_TRUE(base.plan.has_value());

  material.base_color[0] = 0.25F;
  assets.at(0).descriptor_payload = material_bytes();
  assets.at(0).descriptor_sha
    = oxygen::base::ComputeSha256(assets.at(0).descriptor_payload);
  ASSERT_TRUE(
    paktest::WriteLooseIndex(root, std::span(assets).first(1U), {}, 2U));
  request.mode = pak::BuildMode::kPatch;
  request.source_key = MakeNonZeroSourceKey(2U);
  request.output_pak_path = Root() / "p1.pak";
  request.output_manifest_path = Root() / "p1.manifest.json";
  request.base_catalogs = { base.output_catalog };
  const auto p1 = build_and_write();
  ASSERT_TRUE(p1.plan.has_value());
  EXPECT_EQ(p1.output_catalog.deleted, std::vector { assets.at(1).key });
  ASSERT_EQ(p1.output_catalog.entries.size(), 1U);

  assets.at(0).descriptor_payload = original_bytes;
  assets.at(0).descriptor_sha = oxygen::base::ComputeSha256(original_bytes);
  ASSERT_TRUE(
    paktest::WriteLooseIndex(root, std::span(assets).first(1U), {}, 3U));
  request.source_key = MakeNonZeroSourceKey(3U);
  request.output_pak_path = Root() / "p2.pak";
  request.output_manifest_path = Root() / "p2.manifest.json";
  request.base_catalogs.push_back(p1.output_catalog);
  const auto p2 = build_and_write();
  ASSERT_TRUE(p2.plan.has_value());
  ASSERT_EQ(p2.plan->PatchActions().size(), 1U);
  EXPECT_EQ(CheckedAt(p2.plan->PatchActions(), 0U).action,
    pak::PakPatchAction::kReplace);
  EXPECT_EQ(CheckedAt(p2.plan->PatchActions(), 0U).asset_key, assets.at(0).key);
  EXPECT_TRUE(p2.output_catalog.deleted.empty());

  oxygen::content::VirtualPathResolver resolver;
  EXPECT_THROW(resolver.AddPakFile(Root() / "p1.pak"), std::invalid_argument);
  resolver.AddPakFile(Root() / "base.pak");
  EXPECT_EQ(resolver.ResolveAssetKey("/Game/D.omat"), assets.at(1).key);
  resolver.AddPakFile(Root() / "p1.pak");
  EXPECT_FALSE(resolver.ResolveAssetKey("/Game/D.omat").has_value());
  resolver.AddPakFile(Root() / "p2.pak");
  EXPECT_EQ(resolver.ResolveAssetKey("/Game/M.omat"), assets.at(0).key);

  request.mode = pak::BuildMode::kFull;
  request.base_catalogs.clear();
  request.output_manifest_path.clear();
  request.output_pak_path = Root() / "flattened.pak";
  request.source_key = MakeNonZeroSourceKey(4U);
  request.sources = {
    { .kind = data::CookedSourceKind::kPak, .path = Root() / "base.pak" },
    { .kind = data::CookedSourceKind::kPak, .path = Root() / "p1.pak" },
    { .kind = data::CookedSourceKind::kPak, .path = Root() / "p2.pak" },
  };
  const auto flattened = build_and_write();
  ASSERT_TRUE(flattened.plan.has_value());
  const oxygen::content::PakFile archive(request.output_pak_path);
  EXPECT_FALSE(archive.FindEntry(assets.at(1).key).has_value());
  EXPECT_EQ(archive.Catalog().deleted, std::vector { assets.at(1).key });
  const auto entry = archive.FindEntry(assets.at(0).key);
  ASSERT_TRUE(entry.has_value());
  auto reader = archive.CreateReader(*entry);
  const auto bytes = reader.ReadBlob(entry->desc_size);
  ASSERT_TRUE(bytes.has_value());
  EXPECT_EQ(*bytes, original_bytes);
}

NOLINT_TEST_F(
  PakPlanBuilderTest, RepackingRejectsDescriptorBytesThatDisagreeWithCatalog)
{
  const auto root = Root() / "source";
  const auto bytes = paktest::MakeEmptySceneDescriptor();
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
    .source_key = MakeNonZeroSourceKey(1U),
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
  request.source_key = MakeNonZeroSourceKey(2U);
  const auto rejected = pak::PakPlanBuilder {}.Build(request);
  EXPECT_FALSE(rejected.plan.has_value());
  EXPECT_TRUE(HasDiagnosticCode(
    rejected.diagnostics, "pak.plan.catalog_content_mismatch"));
}

NOLINT_TEST_F(PakPlanBuilderTest, FullModeIncludesInputAssetsFromLooseSource)
{
  using data::CookedSource;
  using data::CookedSourceKind;
  using pak::BuildMode;
  using pak::PakPlanBuilder;

  constexpr auto kGuidSeed = uint8_t { 42U };

  const auto source = Root() / "full_include_input_assets";

  const auto action_asset = AssetSpec {
    .key = MakeAssetKey(0x51U),
    .asset_type = data::AssetType::kInputAction,
    .descriptor_relpath = "Descriptors/Input/Move.oiact",
    .virtual_path = "/Game/Input/Move.oiact",
    .descriptor_size = 48U,
    .descriptor_sha = paktest::MakeDigest(0x51U),
  };
  const auto context_asset = AssetSpec {
    .key = MakeAssetKey(0x52U),
    .asset_type = data::AssetType::kInputMappingContext,
    .descriptor_relpath = "Descriptors/Input/Gameplay.oimap",
    .virtual_path = "/Game/Input/Gameplay.oimap",
    .descriptor_size = 64U,
    .descriptor_sha = paktest::MakeDigest(0x52U),
  };
  const auto scene_asset = AssetSpec {
    .key = MakeAssetKey(0x53U),
    .asset_type = data::AssetType::kScene,
    .descriptor_relpath = "Descriptors/Scenes/Main.oscene",
    .virtual_path = "/Game/Scenes/Main.oscene",
    .descriptor_size = paktest::MakeEmptySceneDescriptor().size(),
    .descriptor_sha = paktest::MakeDigest(0x53U),
    .descriptor_payload = paktest::MakeEmptySceneDescriptor(),
  };
  const auto assets
    = std::array<AssetSpec, 3> { action_asset, context_asset, scene_asset };
  ASSERT_TRUE(paktest::WriteLooseIndex(source,
    std::span<const AssetSpec>(assets.data(), assets.size()),
    std::span<const FileSpec> {}, kGuidSeed));

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = { CookedSource {
      .kind = CookedSourceKind::kLooseCooked, .path = source, }, },
    .output_pak_path = Root() / "full_include_input_assets.pak",
    .output_manifest_path = {},
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(0x91U),
    .base_catalogs = {},

    .options = {},
  };

  const auto result = PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(result.diagnostics));
  if (!result.plan.has_value()) {
    FAIL() << "Expected result.plan to contain a value";
  }

  const auto plan_assets = result.plan->Assets();
  ASSERT_EQ(plan_assets.size(), assets.size());

  auto saw_input_action = false;
  auto saw_input_mapping_context = false;
  auto saw_scene = false;
  for (const auto& asset : plan_assets) {
    if (asset.asset_type == data::AssetType::kInputAction) {
      saw_input_action = true;
    } else if (asset.asset_type == data::AssetType::kInputMappingContext) {
      saw_input_mapping_context = true;
    } else if (asset.asset_type == data::AssetType::kScene) {
      saw_scene = true;
    }
  }
  EXPECT_TRUE(saw_input_action);
  EXPECT_TRUE(saw_input_mapping_context);
  EXPECT_TRUE(saw_scene);

  const auto& directory_entries = result.plan->Directory().entries;
  ASSERT_EQ(directory_entries.size(), assets.size());
  auto dir_input_action_count = size_t { 0 };
  auto dir_input_mapping_context_count = size_t { 0 };
  auto dir_scene_count = size_t { 0 };
  for (const auto& entry : directory_entries) {
    if (entry.asset_type == data::AssetType::kInputAction) {
      ++dir_input_action_count;
    } else if (entry.asset_type == data::AssetType::kInputMappingContext) {
      ++dir_input_mapping_context_count;
    } else if (entry.asset_type == data::AssetType::kScene) {
      ++dir_scene_count;
    }
  }
  EXPECT_EQ(dir_input_action_count, 1U);
  EXPECT_EQ(dir_input_mapping_context_count, 1U);
  EXPECT_EQ(dir_scene_count, 1U);
}

NOLINT_TEST_F(PakPlanBuilderTest, IndexZeroPolicyAppliedForResourceTables)
{
  using data::CookedSource;
  using data::CookedSourceKind;
  using pak::BuildMode;
  using pak::PakPlanBuilder;

  constexpr auto kGuidSeed = uint8_t { 41U };
  constexpr auto kDescriptorSize = uint64_t { 24U };

  const auto source = Root() / "resource_tables";
  auto script_asset = AssetSpec {
    .key = MakeAssetKey(0x55U),
    .asset_type = data::AssetType::kScript,
    .descriptor_relpath = "script.desc",
    .virtual_path = "/Game/Script.main",
    .descriptor_size = kDescriptorSize,
    .descriptor_sha = {},
  };
  script_asset.descriptor_sha.at(0) = 0x55U;

  const auto files = std::array<FileSpec, 4> {
    FileSpec {
      .kind = lc::FileKind::kTexturesTable,
      .relpath = "Resources/textures.table",
      .payload = std::vector<std::byte>(sizeof(core::TextureResourceDesc)),
    },
    FileSpec {
      .kind = lc::FileKind::kTexturesData,
      .relpath = "Resources/textures.data",
      .payload = {},
    },
    FileSpec {
      .kind = lc::FileKind::kScriptsTable,
      .relpath = "Resources/scripts.table",
      .payload = std::vector<std::byte>(sizeof(script::ScriptResourceDesc)),
    },
    FileSpec {
      .kind = lc::FileKind::kScriptsData,
      .relpath = "Resources/scripts.data",
      .payload = {},
    },
  };

  ASSERT_TRUE(paktest::WriteLooseIndex(source,
    std::span<const AssetSpec>(&script_asset, 1U),
    std::span<const FileSpec>(files.data(), files.size()), kGuidSeed));

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = { CookedSource {
      .kind = CookedSourceKind::kLooseCooked, .path = source, }, },
    .output_pak_path = Root() / "index0.pak",
    .output_manifest_path = {},
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(11U),
    .base_catalogs = {},

    .options = {},
  };

  const auto result = PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(result.diagnostics));
  if (!result.plan.has_value()) {
    FAIL() << "Expected result.plan to contain a value";
  }

  const auto* texture_table = FindTable(*result.plan, "texture_table");
  const auto* script_resource_table
    = FindTable(*result.plan, "script_resource_table");
  const auto* audio_table = FindTable(*result.plan, "audio_table");
  ASSERT_NE(texture_table, nullptr);
  ASSERT_NE(script_resource_table, nullptr);
  ASSERT_NE(audio_table, nullptr);

  EXPECT_EQ(texture_table->count, 1U);
  EXPECT_TRUE(texture_table->index_zero_required);
  EXPECT_TRUE(texture_table->index_zero_present);
  EXPECT_FALSE(texture_table->index_zero_forbidden);

  EXPECT_EQ(script_resource_table->count, 1U);
  EXPECT_TRUE(script_resource_table->index_zero_required);
  EXPECT_TRUE(script_resource_table->index_zero_present);
  EXPECT_FALSE(script_resource_table->index_zero_forbidden);

  EXPECT_EQ(audio_table->count, 0U);
  EXPECT_FALSE(audio_table->index_zero_required);
  EXPECT_FALSE(audio_table->index_zero_present);
}

NOLINT_TEST_F(
  PakPlanBuilderTest, BrowsePayloadPlanSizeMatchesSerializedShapeInvariant)
{
  using data::CookedSource;
  using data::CookedSourceKind;
  using pak::BuildMode;
  using pak::PakPlanBuilder;

  constexpr auto kGuidSeed = uint8_t { 53U };
  constexpr auto kDescriptorSize = uint64_t { 20U };

  const auto source = Root() / "browse_payload";
  auto asset_a = AssetSpec {
    .key = MakeAssetKey(0x61U),
    .asset_type = data::AssetType::kMaterial,
    .descriptor_relpath = "a.desc",
    .virtual_path = "/Game/Browse/B.asset",
    .descriptor_size = kDescriptorSize,
    .descriptor_sha = {},
  };
  asset_a.descriptor_sha.at(0) = 0x61U;

  auto asset_b = AssetSpec {
    .key = MakeAssetKey(0x62U),
    .asset_type = data::AssetType::kScript,
    .descriptor_relpath = "b.desc",
    .virtual_path = "/Game/Browse/A.asset",
    .descriptor_size = kDescriptorSize,
    .descriptor_sha = {},
  };
  asset_b.descriptor_sha.at(0) = 0x62U;

  const auto assets = std::array<AssetSpec, 2> { asset_a, asset_b };
  ASSERT_TRUE(paktest::WriteLooseIndex(source,
    std::span<const AssetSpec>(assets.data(), assets.size()),
    std::span<const FileSpec> {}, kGuidSeed));

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources
    = { CookedSource { .kind = CookedSourceKind::kLooseCooked, .path = source } },
    .output_pak_path = Root() / "browse_payload.pak",
    .output_manifest_path = {},
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(0xB2U),
    .base_catalogs = {},

    .options = {
      .deterministic = true,
      .embed_browse_index = true,
      .emit_manifest_in_full = false,
      .compute_crc32 = true,
      .fail_on_warnings = false,
    },
  };

  const auto result = PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(result.diagnostics));
  if (!result.plan.has_value()) {
    FAIL() << "Expected result.plan to contain a value";
  }

  const auto& browse = result.plan->BrowseIndex();
  ASSERT_TRUE(browse.enabled);
  ASSERT_EQ(browse.entries.size(), 2U);
  EXPECT_LT(
    browse.entries.at(0).virtual_path, browse.entries.at(1).virtual_path);

  const auto expected_payload_size
    = ComputeExpectedBrowsePayloadSize(std::span<const pak::PakBrowseEntryPlan>(
      browse.entries.data(), browse.entries.size()));
  EXPECT_EQ(browse.size_bytes, expected_payload_size);
  EXPECT_FALSE(HasDiagnosticCode(
    result.diagnostics, "pak.plan.stage.layout.browse_store_failed"));
  EXPECT_FALSE(HasDiagnosticCode(
    result.diagnostics, "pak.plan.stage.layout.browse_size_mismatch"));
}

NOLINT_TEST_F(PakPlanBuilderTest, SceneMaskIndicesRemapAcrossCookedSources)
{
  namespace world = oxygen::data::pak::world;
  namespace serio = oxygen::serio;
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
  ASSERT_TRUE(scene_writer.Write(descriptor));
  ASSERT_TRUE(scene_writer.Write(environment));
  ASSERT_TRUE(serio::Store(scene_writer, post));
  auto scene_bytes = std::vector<std::byte>(
    scene_stream.Data().begin(), scene_stream.Data().end());
  EnableDescriptorHash(scene_bytes);

  serio::MemoryStream texture_stream;
  serio::Writer texture_writer(texture_stream);
  const auto texture_packed = texture_writer.ScopedAlignment(1);
  ASSERT_TRUE(texture_writer.Write(core::TextureResourceDesc {}));
  ASSERT_TRUE(texture_writer.Write(core::TextureResourceDesc {
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
  auto sources = std::vector<data::CookedSource> {};
  for (const auto seed : { uint8_t { 1U }, uint8_t { 2U } }) {
    const auto root = Root() / std::to_string(seed);
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
          { .kind = data::ResourceKind::kTexture,
            .index = oxygen::ResourceIndexT { 1U } },
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
    auto material = render::MaterialAssetDesc {};
    material.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kMaterial);
    material.header.version = render::kMaterialAssetVersion;
    material.base_color_texture = data::ResourceReferenceIndex { 0U };
    serio::MemoryStream material_stream;
    serio::Writer material_writer(material_stream);
    const auto material_packed = material_writer.ScopedAlignment(1);
    ASSERT_TRUE(
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
          { .kind = data::ResourceKind::kTexture,
            .index = oxygen::ResourceIndexT { 1U } },
        },
        {})
        .value(),
    };
    const auto sidecar = MakePhysicsSidecarSpec(seed, asset);
    const auto source_assets = std::array { asset, material_asset, sidecar };
    ASSERT_TRUE(paktest::WriteLooseIndex(root, source_assets, files, seed));
    sources.push_back(
      { .kind = data::CookedSourceKind::kLooseCooked, .path = root });
  }
  const auto request = pak::PakBuildRequest {
    .mode = pak::BuildMode::kFull,
    .sources = sources,
    .output_pak_path = Root() / "masks.pak",
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(3U),
  };
  const auto result = pak::PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(result.diagnostics));
  if (!result.plan.has_value()) {
    FAIL() << "Expected result.plan to contain a value";
  }
  const auto assets = result.plan->Assets();
  const auto payloads = result.plan->AssetPayloadSources();
  ASSERT_EQ(assets.size(), 6U);
  bool found_second_scene = false;
  for (size_t index = 0; index < assets.size(); ++index) {
    if (CheckedAt(assets, index).asset_key != MakeAssetKey(20U)) {
      continue;
    }
    EXPECT_TRUE(CheckedAt(payloads, index).inline_bytes.empty());
    EXPECT_EQ(CheckedAt(payloads, index).size_bytes, scene_bytes.size());
    found_second_scene = true;
    break;
  }
  ASSERT_TRUE(found_second_scene);
  const auto written = pak::PakWriter {}.Write(request, *result.plan);
  ASSERT_FALSE(HasError(written.diagnostics));

  auto unchanged_request = request;
  unchanged_request.mode = pak::BuildMode::kPatch;
  unchanged_request.sources = {
    {
      .kind = data::CookedSourceKind::kPak,
      .path = request.output_pak_path,
    },
  };
  unchanged_request.output_pak_path = Root() / "unchanged.pak";
  unchanged_request.output_manifest_path = Root() / "unchanged.manifest.json";
  unchanged_request.source_key = MakeNonZeroSourceKey(6U);
  unchanged_request.base_catalogs = { result.output_catalog };
  const auto unchanged = pak::PakPlanBuilder {}.Build(unchanged_request);
  ASSERT_FALSE(HasError(unchanged.diagnostics));
  ASSERT_TRUE(unchanged.plan.has_value());
  EXPECT_TRUE(unchanged.plan->Assets().empty());

  for (const auto mode : { pak::BuildMode::kFull, pak::BuildMode::kPatch }) {
    auto repack = request;
    repack.mode = mode;
    repack.sources = {
      {
        .kind = data::CookedSourceKind::kPak,
        .path = request.output_pak_path,
      },
    };
    repack.output_pak_path
      = Root() / (mode == pak::BuildMode::kFull ? "repacked.pak" : "patch.pak");
    repack.source_key = MakeNonZeroSourceKey(4U);
    if (mode == pak::BuildMode::kPatch) {
      repack.output_manifest_path = Root() / "patch.manifest.json";
      repack.base_catalogs.push_back(data::PakCatalog {
        .source_key = MakeNonZeroSourceKey(5U),
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

NOLINT_TEST_F(
  PakPlanBuilderTest, LayeredPhysicsPayloadUsesWinningSourceInFullAndPatch)
{
  const auto owner_root = Root() / "owner";
  const auto library_root = Root() / "library";
  const auto material_key = MakeAssetKey(1U);
  const auto shape_key = MakeAssetKey(2U);
  const auto payload_key = MakeAssetKey(3U);
  const auto bytes_of = [](const auto& value) {
    const auto bytes = std::as_bytes(std::span { &value, 1U });
    return std::vector<std::byte>(bytes.begin(), bytes.end());
  };
  auto material = physics::PhysicsMaterialAssetDesc {};
  material.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kPhysicsMaterial);
  material.header.version = physics::kPhysicsMaterialAssetVersion;
  auto shape = physics::CollisionShapeAssetDesc {};
  shape.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kCollisionShape);
  shape.header.version = physics::kCollisionShapeAssetVersion;
  shape.shape_type = physics::ShapeType::kConvexHull;
  shape.material_asset_key = material_key;
  shape.cooked_shape_ref.payload_asset_key = payload_key;
  shape.cooked_shape_ref.payload_type = physics::ShapePayloadType::kConvex;
  auto shape_bytes = bytes_of(shape);
  auto assets = std::array {
    AssetSpec {
      .key = material_key,
      .asset_type = data::AssetType::kPhysicsMaterial,
      .descriptor_relpath = "Material.opmat",
      .virtual_path = "/Game/Material.opmat",
      .descriptor_size = sizeof(material),
      .descriptor_sha = oxygen::base::ComputeSha256(bytes_of(material)),
      .descriptor_payload = bytes_of(material),
      .references = {},
    },
    AssetSpec {
      .key = shape_key,
      .asset_type = data::AssetType::kCollisionShape,
      .descriptor_relpath = "Shape.ocshape",
      .virtual_path = "/Game/Shape.ocshape",
      .descriptor_size = sizeof(shape),
      .descriptor_sha = oxygen::base::ComputeSha256(shape_bytes),
      .descriptor_payload = shape_bytes,
      .references = data::AssetReferences::Create({},
        {
          { .key = material_key,
            .kind = data::KeyReferenceKind::kAsset,
            .expected_type = data::AssetType::kPhysicsMaterial },
          { .key = payload_key,
            .kind = data::KeyReferenceKind::kPhysicsResource,
            .expected_type = data::AssetType::kUnknown },
        })
        .value(),
    },
  };
  auto record = physics::PhysicsResourceDesc {};
  record.resource_asset_key = payload_key;
  record.size_bytes = 4U;
  const auto table = std::array { physics::PhysicsResourceDesc {}, record };
  const auto table_bytes = std::as_bytes(std::span(table));
  auto files = std::array {
    FileSpec { .kind = lc::FileKind::kPhysicsTable,
      .relpath = "physics.table",
      .payload = { table_bytes.begin(), table_bytes.end() } },
    FileSpec { .kind = lc::FileKind::kPhysicsData,
      .relpath = "physics.data",
      .payload = std::vector<std::byte>(4U, std::byte { 1 }) },
  };
  ASSERT_TRUE(paktest::WriteLooseIndex(owner_root, assets, files, 1U));
  files.at(1).payload.assign(4U, std::byte { 2 });
  ASSERT_TRUE(paktest::WriteLooseIndex(library_root, {}, files, 2U));
  auto request = pak::PakBuildRequest {
    .mode = pak::BuildMode::kFull,
    .sources = {
      { .kind = data::CookedSourceKind::kLooseCooked, .path = owner_root },
      { .kind = data::CookedSourceKind::kLooseCooked, .path = library_root },
    },
    .output_pak_path = Root() / "physics.pak",
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(3U),
  };
  const auto full = pak::PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(full.diagnostics));
  ASSERT_TRUE(full.plan.has_value());
  const auto assert_payload = [&](const pak::PakPlan& plan) {
    size_t payload_count = 0;
    const auto resources = plan.Resources();
    for (size_t index = 0; index < resources.size(); ++index) {
      const auto& resource = CheckedAt(resources, index);
      if (resource.resource_kind != "physics" || resource.size_bytes == 0U) {
        continue;
      }
      ++payload_count;
      const auto& payload = CheckedAt(plan.ResourcePayloadSources(), index);
      EXPECT_EQ(payload.source_path, library_root / "physics.data");
      EXPECT_EQ(payload.size_bytes, 4U);
    }
    EXPECT_EQ(payload_count, 1U);
  };
  assert_payload(*full.plan);
  EXPECT_FALSE(
    HasError(pak::PakWriter {}.Write(request, *full.plan).diagnostics));
  {
    const oxygen::content::PakFile packed(request.output_pak_path);
    EXPECT_EQ(packed.PhysicsTable().Size().get(), 2U);
  }
  oxygen::co::testing::TestEventLoop loop;
  oxygen::co::Run(loop,
    CheckPackagedPhysics(&loop, request.output_pak_path, request.source_key));

  // Only the shape changes. Its payload owner has no emitted descriptor.
  shape.is_sensor = physics::kShapeIsSensorTrue;
  shape_bytes = bytes_of(shape);
  assets.at(1).descriptor_payload = shape_bytes;
  assets.at(1).descriptor_sha = oxygen::base::ComputeSha256(shape_bytes);
  files.at(1).payload.assign(4U, std::byte { 1 });
  ASSERT_TRUE(paktest::WriteLooseIndex(owner_root, assets, files, 1U));
  request.mode = pak::BuildMode::kPatch;
  request.base_catalogs = { full.output_catalog };
  request.source_key = MakeNonZeroSourceKey(4U);
  request.output_manifest_path = Root() / "physics.manifest.json";
  const auto patch = pak::PakPlanBuilder {}.Build(request);
  for (const auto& diagnostic : patch.diagnostics) {
    EXPECT_NE(diagnostic.severity, pak::PakDiagnosticSeverity::kError)
      << diagnostic.code << ": " << diagnostic.message;
  }
  ASSERT_FALSE(HasError(patch.diagnostics));
  ASSERT_TRUE(patch.plan.has_value());
  ASSERT_EQ(patch.plan->Assets().size(), 1U);
  EXPECT_EQ(CheckedAt(patch.plan->Assets(), 0U).asset_key, shape_key);
  assert_payload(*patch.plan);
  const auto resources = patch.plan->Resources();
  const auto payload
    = std::ranges::find_if(resources, [](const auto& resource) {
        return resource.resource_kind == "physics" && resource.size_bytes == 4U;
      });
  ASSERT_NE(payload, resources.end());
  EXPECT_EQ(std::ranges::count_if(patch.plan->PatchClosure(),
              [&](const auto& dependency) {
                return dependency.asset_key == shape_key
                  && dependency.resource_kind == "physics"
                  && dependency.resource_index == payload->resource_index;
              }),
    1);
}

NOLINT_TEST_F(PakPlanBuilderTest, DeletedSidecarIsAbsentInNewLoadScope)
{
  const auto bytes = paktest::MakeEmptySceneDescriptor();
  const auto scene = AssetSpec { .key = MakeAssetKey(10U),
    .asset_type = data::AssetType::kScene,
    .descriptor_relpath = "Scene.oscene",
    .virtual_path = "/Game/Scene.oscene",
    .descriptor_size = bytes.size(),
    .descriptor_sha = oxygen::base::ComputeSha256(bytes),
    .descriptor_payload = bytes,
    .references = {} };
  auto sidecar = MakePhysicsSidecarSpec(11U, scene);
  sidecar.references = data::AssetReferences::Create({},
    { { .key = scene.key,
      .kind = data::KeyReferenceKind::kLogical,
      .expected_type = data::AssetType::kScene } })
                         .value();
  const auto root = Root() / "source";
  ASSERT_TRUE(
    paktest::WriteLooseIndex(root, std::array { scene, sidecar }, {}, 1U));
  auto request = pak::PakBuildRequest { .mode = pak::BuildMode::kFull,
    .sources
    = { { .kind = data::CookedSourceKind::kLooseCooked, .path = root } },
    .output_pak_path = Root() / "base.pak",
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(20U) };
  const auto base = pak::PakPlanBuilder {}.Build(request);
  ASSERT_TRUE(base.plan);
  ASSERT_FALSE(
    HasError(pak::PakWriter {}.Write(request, *base.plan).diagnostics));
  oxygen::content::AssetLoader loader(
    oxygen::engine::internal::EngineTagFactory::Get());
  loader.AddPakFile(request.output_pak_path);
  const auto original = loader.BeginLoadScope();
  const data::SceneAsset scene_asset(scene.key, bytes);
  EXPECT_EQ(loader.FindPhysicsSidecarAssetKeyForScene(scene_asset, original),
    sidecar.key);
  ASSERT_TRUE(paktest::WriteLooseIndex(root, std::array { scene }, {}, 1U));
  request.mode = pak::BuildMode::kPatch;
  request.output_pak_path = Root() / "patch.pak";
  request.output_manifest_path = Root() / "patch.manifest.json";
  request.source_key = MakeNonZeroSourceKey(21U);
  request.base_catalogs = { base.output_catalog };
  const auto patch = pak::PakPlanBuilder {}.Build(request);
  ASSERT_TRUE(patch.plan);
  ASSERT_FALSE(
    HasError(pak::PakWriter {}.Write(request, *patch.plan).diagnostics));
  loader.AddPakFile(request.output_pak_path);
  EXPECT_FALSE(loader.FindPhysicsSidecarAssetKeyForScene(
    scene_asset, loader.BeginLoadScope()));
  EXPECT_EQ(loader.FindPhysicsSidecarAssetKeyForScene(scene_asset, original),
    sidecar.key);
}

NOLINT_TEST_F(PakPlanBuilderTest, RejectsInvalidPhysicsScenePairs)
{
  const auto scene_bytes = paktest::MakeEmptySceneDescriptor();
  const auto scene = AssetSpec {
    .key = MakeAssetKey(10U),
    .asset_type = data::AssetType::kScene,
    .descriptor_relpath = "Scene.oscene",
    .virtual_path = "/Game/Scene.oscene",
    .descriptor_size = scene_bytes.size(),
    .descriptor_sha = oxygen::base::ComputeSha256(scene_bytes),
    .descriptor_payload = scene_bytes,
  };
  const auto source = Root() / "invalid-pair";
  const auto request = pak::PakBuildRequest {
    .mode = pak::BuildMode::kFull,
    .sources
    = { { .kind = data::CookedSourceKind::kLooseCooked, .path = source } },
    .output_pak_path = Root() / "invalid-pair.pak",
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(2U),
  };
  const auto expect_rejected
    = [&](const std::span<const AssetSpec> assets) -> void {
    ASSERT_TRUE(paktest::WriteLooseIndex(source, assets, {}, 1U));
    const auto result = pak::PakPlanBuilder {}.Build(request);
    EXPECT_FALSE(result.plan.has_value());
    EXPECT_TRUE(HasDiagnosticCode(
      result.diagnostics, "pak.plan.physics_scene_pair_invalid"));
  };
  auto sidecar = MakePhysicsSidecarSpec(1U, scene);
  sidecar.descriptor_payload.at(offsetof(physics::PhysicsSceneAssetDesc,
    target_scene_content_hash)) ^= std::byte { 1 };
  expect_rejected(std::array { scene, sidecar });

  sidecar = MakePhysicsSidecarSpec(1U, scene);
  auto duplicate = MakePhysicsSidecarSpec(2U, scene);
  duplicate.descriptor_relpath = "Duplicate.opscene";
  expect_rejected(std::array { scene, sidecar, duplicate });

  sidecar.descriptor_payload.at(offsetof(
    physics::PhysicsSceneAssetDesc, target_node_count)) = std::byte { 1 };
  expect_rejected(std::array { scene, sidecar });
}

NOLINT_TEST_F(PakPlanBuilderTest, PatchIncludesBothMembersOfPhysicsScenePair)
{
  const auto scene_bytes = paktest::MakeEmptySceneDescriptor();
  const auto scene = AssetSpec {
    .key = MakeAssetKey(10U),
    .asset_type = data::AssetType::kScene,
    .descriptor_relpath = "Scene.oscene",
    .virtual_path = "/Game/Scene.oscene",
    .descriptor_size = scene_bytes.size(),
    .descriptor_sha = oxygen::base::ComputeSha256(scene_bytes),
    .descriptor_payload = scene_bytes,
  };
  const auto sidecar = MakePhysicsSidecarSpec(1U, scene);
  const auto source = Root() / "pair";
  ASSERT_TRUE(
    paktest::WriteLooseIndex(source, std::array { scene, sidecar }, {}, 1U));
  auto request = pak::PakBuildRequest {
    .mode = pak::BuildMode::kFull,
    .sources
    = { { .kind = data::CookedSourceKind::kLooseCooked, .path = source } },
    .output_pak_path = Root() / "pair.pak",
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(2U),
  };
  const auto baseline = pak::PakPlanBuilder {}.Build(request);
  ASSERT_TRUE(baseline.plan.has_value());
  request.mode = pak::BuildMode::kPatch;
  request.source_key = MakeNonZeroSourceKey(3U);
  request.output_manifest_path = Root() / "patch.manifest.json";
  for (const auto changed_key : { scene.key, sidecar.key }) {
    auto base = baseline.output_catalog;
    const auto changed = std::ranges::find(
      base.entries, changed_key, &data::PakCatalogEntry::asset_key);
    ASSERT_NE(changed, base.entries.end());
    changed->descriptor_digest.front() ^= 1U;
    base.catalog_digest = base.ComputeDigest().value();
    request.base_catalogs = { std::move(base) };
    const auto patch = pak::PakPlanBuilder {}.Build(request);
    for (const auto& diagnostic : patch.diagnostics) {
      EXPECT_NE(diagnostic.severity, pak::PakDiagnosticSeverity::kError)
        << diagnostic.message;
    }
    if (!patch.plan.has_value()) {
      FAIL() << "Expected patch.plan to contain a value";
    }
    EXPECT_EQ(patch.plan->Assets().size(), 2U);
    for (const auto& action : patch.plan->PatchActions()) {
      EXPECT_EQ(action.action, pak::PakPatchAction::kReplace);
    }
  }
}

} // namespace
