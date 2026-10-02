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
#include <filesystem>
#include <initializer_list>
#include <ios>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "PakTestSupport.h"

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Cooker/Pak/PakBuildReport.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakMeasureStore.h>
#include <Oxygen/Cooker/Pak/PakPlan.h>
#include <Oxygen/Cooker/Pak/PakPlanBuilder.h>
#include <Oxygen/Cooker/Pak/PakWriter.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormatSerioLoaders.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace base = oxygen::base;
namespace data = oxygen::data;
namespace lc = oxygen::data::loose_cooked;
namespace pak = oxygen::content::pak;
namespace paktest = oxygen::content::pak::test;
namespace core = oxygen::data::pak::core;
namespace render = oxygen::data::pak::render;
namespace script = oxygen::data::pak::scripting;
namespace world = oxygen::data::pak::world;

constexpr auto kDigestPrimaryByteIndex = size_t { 0U };

using paktest::AssetSpec;
using paktest::FileSpec;

auto MakeSourceKey(const uint8_t seed) -> data::SourceKey
{
  return paktest::MakeSourceKey(seed);
}

auto MakeAssetKey(const uint8_t seed) -> data::AssetKey
{
  return paktest::MakeAssetKey(seed);
}

auto MakeDigest(const uint8_t seed) -> base::Sha256Digest
{
  auto digest = base::Sha256Digest {};
  digest.fill(0U);
  digest[kDigestPrimaryByteIndex] = seed;
  return digest;
}

auto ToLooseSha(const base::Sha256Digest& digest)
  -> std::array<uint8_t, lc::kSha256Size>
{
  auto out = std::array<uint8_t, lc::kSha256Size> {};
  std::ranges::copy(digest, out.begin());
  return out;
}

auto BuildMaterialDescriptor(const std::string_view name)
  -> std::vector<std::byte>
{
  auto desc = render::MaterialAssetDesc {};
  desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kMaterial);
  desc.header.version = render::kMaterialAssetVersion;
  const auto max_name_bytes = sizeof(desc.header.name) - 1U;
  const auto name_bytes = std::min(name.size(), max_name_bytes);
  std::memcpy(desc.header.name, name.data(), name_bytes);
  desc.header.name[name_bytes] = '\0';
  desc.base_color_texture = data::ResourceReferenceIndex { 0U };

  auto bytes = std::vector<std::byte>(sizeof(desc));
  std::memcpy(bytes.data(), std::addressof(desc), sizeof(desc));
  return bytes;
}

auto BuildTextureRecord(const uint64_t data_offset, const uint32_t size_bytes)
  -> core::TextureResourceDesc
{
  auto record = core::TextureResourceDesc {};
  record.data_offset = data_offset;
  record.size_bytes = size_bytes;
  record.texture_type = 1U;
  record.compression_type = 0U;
  record.width = 1U;
  record.height = 1U;
  record.depth = 1U;
  record.array_layers = 1U;
  record.mip_levels = 1U;
  record.format = 0U;
  record.alignment = 256U;
  return record;
}

auto AggregateTaggedDigests(
  std::span<const std::pair<uint16_t, base::Sha256Digest>> inputs)
  -> base::Sha256Digest
{
  if (inputs.empty()) {
    return base::ComputeSha256(std::span<const std::byte> {});
  }

  auto sorted = std::vector<std::pair<uint16_t, base::Sha256Digest>>(
    inputs.begin(), inputs.end());
  std::ranges::sort(sorted, [](const auto& lhs, const auto& rhs) -> auto {
    if (lhs.first != rhs.first) {
      return lhs.first < rhs.first;
    }
    return lhs.second < rhs.second;
  });

  auto hasher = base::Sha256 {};
  for (const auto& [kind, digest] : sorted) {
    hasher.Update(std::as_bytes(std::span(&kind, 1)));
    hasher.Update(std::as_bytes(std::span(digest)));
  }
  return hasher.Finalize();
}

auto ComputeNormalizedTextureDigest(const core::TextureResourceDesc& record,
  std::span<const std::byte> payload) -> base::Sha256Digest
{
  auto normalized = record;
  normalized.data_offset = 0U;

  auto hasher = base::Sha256 {};
  hasher.Update(std::as_bytes(std::span(&normalized, 1)));
  if (!payload.empty()) {
    hasher.Update(payload);
  }
  return hasher.Finalize();
}

auto EmptyDigest() -> base::Sha256Digest
{
  static const auto kEmptyDigest
    = base::ComputeSha256(std::span<const std::byte> {});
  return kEmptyDigest;
}

auto BuildSceneDescriptorWithScriptingSlots(const uint8_t seed)
  -> std::vector<std::byte>
{
  auto desc = world::SceneAssetDesc {};
  desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kScene);
  desc.header.version = world::kSceneAssetVersion;
  std::array<world::NodeRecord, 2> nodes {};
  nodes.front().node_id = MakeAssetKey(seed);
  nodes.back().node_id = MakeAssetKey(static_cast<uint8_t>(seed + 2U));
  nodes.front().parent_index = 0U;
  nodes.back().parent_index = 0U;
  desc.nodes = {
    .offset = sizeof(desc), .count = 2U, .entry_size = sizeof(world::NodeRecord)
  };
  desc.scene_strings.offset = sizeof(desc) + sizeof(nodes);
  desc.scene_strings.size = 1U;
  desc.component_table_directory_offset = desc.scene_strings.offset + 1U;
  desc.component_table_count = 1U;
  auto table = world::SceneComponentTableDesc {};
  table.component_type = static_cast<uint32_t>(data::ComponentType::kScripting);
  table.table
    = { .offset = desc.component_table_directory_offset + sizeof(table),
        .count = 2U,
        .entry_size = sizeof(script::ScriptingComponentRecord) };
  std::array<script::ScriptingComponentRecord, 2> components {};
  std::array<script::ScriptSlotRecord, 2> slots {};
  std::array<script::ScriptParamRecord, 2> parameters {};
  desc.script_slots = { .offset = table.table.offset + sizeof(components),
    .count = 2U,
    .entry_size = sizeof(script::ScriptSlotRecord) };
  for (size_t i = 0; i < slots.size(); ++i) {
    auto& component = components.at(i);
    component.node_index = static_cast<uint32_t>(i);
    component.slot_start_index = static_cast<uint32_t>(i);
    component.slot_count = 1U;
    auto& slot = slots.at(i);
    slot.script_asset_key = MakeAssetKey(static_cast<uint8_t>(seed * 10U + i));
    slot.params_array_offset = desc.script_slots.offset + sizeof(slots)
      + i * sizeof(script::ScriptParamRecord);
    slot.params_count = 1U;
    auto& parameter = parameters.at(i);
    parameter.key[0] = 'v';
    parameter.type = script::ScriptParamType::kInt32;
    parameter.value.as_int32 = seed * 100 + static_cast<int32_t>(i);
  }
  world::SceneEnvironmentBlockHeader environment {};
  environment.byte_size = sizeof(environment);
  std::vector<std::byte> bytes;
  const auto append = [&bytes](const auto& record) {
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

auto MakeSceneScriptAssets(const uint8_t seed) -> std::vector<AssetSpec>
{
  const auto scene_bytes = BuildSceneDescriptorWithScriptingSlots(seed);
  std::vector<AssetSpec> assets;
  std::vector<data::KeyReference> keys;
  for (uint8_t i = 0U; i < 2U; ++i) {
    const auto key = MakeAssetKey(static_cast<uint8_t>(seed * 10U + i));
    keys.push_back({ .key = key,
      .kind = data::KeyReferenceKind::kAsset,
      .expected_type = data::AssetType::kScript });
    script::ScriptAssetDesc script_desc {};
    script_desc.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kScript);
    script_desc.header.version = script::kScriptAssetVersion;
    const auto bytes = std::as_bytes(std::span(&script_desc, 1U));
    const auto name
      = "Logic" + std::to_string(seed) + "-" + std::to_string(i) + ".oscript";
    assets.push_back({ .key = key,
      .asset_type = data::AssetType::kScript,
      .descriptor_relpath = name,
      .virtual_path = "/Game/" + name,
      .descriptor_size = bytes.size(),
      .descriptor_sha = {},
      .descriptor_payload = { bytes.begin(), bytes.end() },
      .references = {} });
  }
  assets.push_back({ .key = MakeAssetKey(seed),
    .asset_type = data::AssetType::kScene,
    .descriptor_relpath = "Scene.oscene",
    .virtual_path = "/Game/Scene" + std::to_string(seed) + ".oscene",
    .descriptor_size = scene_bytes.size(),
    .descriptor_sha = {},
    .descriptor_payload = scene_bytes,
    .references = data::AssetReferences::Create({}, std::move(keys)).value() });
  return assets;
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

auto DiagnosticSummary(std::span<const pak::PakDiagnostic> diagnostics)
  -> std::string
{
  auto text = std::string {};
  for (const auto& diagnostic : diagnostics) {
    if (!text.empty()) {
      text += " | ";
    }
    text += diagnostic.code;
    text += ": ";
    text += diagnostic.message;
  }
  return text;
}

auto FindAction(const pak::PakPlan& plan, const data::AssetKey& key)
  -> const pak::PakPatchActionRecord*
{
  const auto actions = plan.PatchActions();
  const auto it = std::ranges::find_if(
    actions, [&key](const pak::PakPatchActionRecord& record) -> bool {
      return record.asset_key == key;
    });
  if (it == actions.end()) {
    return nullptr;
  }
  return std::addressof(*it);
}

auto ContainsAsset(const pak::PakPlan& plan, const data::AssetKey& key) -> bool
{
  return std::ranges::any_of(
    plan.Assets(), [&key](const pak::PakAssetPlacementPlan& asset) -> bool {
      return asset.asset_key == key;
    });
}

auto ShouldContributeToTransitiveDigest(const lc::FileKind kind) -> bool
{
  switch (kind) {
  case lc::FileKind::kTexturesTable:
  case lc::FileKind::kTexturesData:
  case lc::FileKind::kBuffersTable:
  case lc::FileKind::kBuffersData:
  case lc::FileKind::kScriptsTable:
  case lc::FileKind::kScriptsData:
  case lc::FileKind::kPhysicsTable:
  case lc::FileKind::kPhysicsData:
    return true;
  case lc::FileKind::kUnknown:
    return false;
  }

  return false;
}

auto ComputeTransitiveDigestFromFiles(std::span<const FileSpec> files)
  -> base::Sha256Digest
{
  auto inputs = std::vector<std::pair<uint16_t, base::Sha256Digest>> {};
  for (const auto& file : files) {
    if (!ShouldContributeToTransitiveDigest(file.kind)) {
      continue;
    }
    const auto digest = base::ComputeSha256(
      std::span<const std::byte>(file.payload.data(), file.payload.size()));
    inputs.emplace_back(static_cast<uint16_t>(file.kind), digest);
  }

  if (inputs.empty()) {
    return EmptyDigest();
  }

  std::ranges::sort(inputs, [](const auto& lhs, const auto& rhs) -> auto {
    if (lhs.first != rhs.first) {
      return lhs.first < rhs.first;
    }
    return lhs.second < rhs.second;
  });

  auto hasher = base::Sha256 {};
  for (const auto& [kind, digest] : inputs) {
    hasher.Update(std::as_bytes(std::span(&kind, 1)));
    hasher.Update(std::as_bytes(std::span(digest)));
  }
  return hasher.Finalize();
}

auto MakeCatalogEntry(const data::AssetKey& key, const data::AssetType type,
  const base::Sha256Digest& descriptor_digest,
  const base::Sha256Digest& transitive_digest) -> data::PakCatalogEntry
{
  return data::PakCatalogEntry {
    .asset_key = key,
    .asset_type = type,
    .descriptor_digest = descriptor_digest,
    .transitive_resource_digest = transitive_digest,
  };
}

auto MakeBaseCatalog(std::span<const data::PakCatalogEntry> entries)
  -> data::PakCatalog
{
  constexpr auto kBaseCatalogSourceSeed = uint8_t { 0xC1U };
  constexpr auto kBaseCatalogContentVersion = uint16_t { 9U };

  auto catalog = data::PakCatalog {
    .source_key = MakeSourceKey(kBaseCatalogSourceSeed),
    .content_version = kBaseCatalogContentVersion,
    .catalog_digest = {},
    .entries
    = std::vector<data::PakCatalogEntry>(entries.begin(), entries.end()),
  };
  catalog.catalog_digest = catalog.ComputeDigest().value();
  return catalog;
}

auto MakePatchRequest(const std::filesystem::path& pak_path,
  std::initializer_list<data::CookedSource> sources,
  std::span<const data::PakCatalog> base_catalogs) -> pak::PakBuildRequest
{
  constexpr auto kPatchContentVersion = uint16_t { 42U };
  constexpr auto kPatchSourceSeed = uint8_t { 0xA5U };

  auto manifest_path = pak_path;
  manifest_path.replace_extension(".manifest");

  return pak::PakBuildRequest {
    .mode = pak::BuildMode::kPatch,
    .sources = std::vector<data::CookedSource>(sources.begin(), sources.end()),
    .output_pak_path = pak_path,
    .output_manifest_path = manifest_path,
    .content_version = kPatchContentVersion,
    .source_key = MakeSourceKey(kPatchSourceSeed),
    .base_catalogs = std::vector<data::PakCatalog>(
      base_catalogs.begin(), base_catalogs.end()),

    .options = {
      .deterministic = true,
      .embed_browse_index = false,
      .emit_manifest_in_full = false,
      .compute_crc32 = true,
      .fail_on_warnings = false,
    },
  };
}

auto PatchActionSignature(const pak::PakPlan& plan)
  -> std::vector<std::pair<data::AssetKey, pak::PakPatchAction>>
{
  auto signature
    = std::vector<std::pair<data::AssetKey, pak::PakPatchAction>> {};
  signature.reserve(plan.PatchActions().size());
  for (const auto& action : plan.PatchActions()) {
    signature.emplace_back(action.asset_key, action.action);
  }
  std::ranges::sort(signature, [](const auto& lhs, const auto& rhs) -> auto {
    return lhs.first < rhs.first;
  });
  return signature;
}

class PakPatchPlannerTest : public paktest::TempDirFixture { };

NOLINT_TEST_F(PakPatchPlannerTest,
  PatchClassificationIsDeterministicAndEmitsOnlyCreateReplaceDescriptors)
{
  using data::CookedSource;
  using data::CookedSourceKind;

  constexpr auto kDescSize = uint64_t { 24U };
  constexpr auto kCreateAssetSeed = uint8_t { 0x10U };
  constexpr auto kReplaceAssetSeed = uint8_t { 0x11U };
  constexpr auto kUnchangedAssetSeed = uint8_t { 0x12U };
  constexpr auto kDeleteAssetSeed = uint8_t { 0x13U };
  constexpr auto kCreateDescriptorSeed = uint8_t { 0x31U };
  constexpr auto kReplaceDescriptorSeed = uint8_t { 0x32U };
  constexpr auto kBaseReplaceDescriptorSeed = uint8_t { 0x52U };
  constexpr auto kUnchangedDescriptorSeed = uint8_t { 0x33U };
  constexpr auto kDeleteDescriptorSeed = uint8_t { 0x44U };
  constexpr auto kTexturePayloadBytes = size_t { 19U };
  constexpr auto kBufferPayloadBytes = size_t { 17U };
  constexpr auto kCreateGuidSeed = uint8_t { 1U };
  constexpr auto kReplaceGuidSeed = uint8_t { 2U };
  constexpr auto kUnchangedGuidSeed = uint8_t { 3U };

  const auto source_create = Root() / "a_create";
  const auto source_replace = Root() / "b_replace";
  const auto source_unchanged = Root() / "c_unchanged";

  const auto create_key = MakeAssetKey(kCreateAssetSeed);
  const auto replace_key = MakeAssetKey(kReplaceAssetSeed);
  const auto unchanged_key = MakeAssetKey(kUnchangedAssetSeed);
  const auto delete_key = MakeAssetKey(kDeleteAssetSeed);

  auto create_asset = AssetSpec {
    .key = create_key,
    .asset_type = data::AssetType::kMaterial,
    .descriptor_relpath = "create.desc",
    .virtual_path = "/Game/Patch/Create.asset",
    .descriptor_size = kDescSize,
    .descriptor_sha = {},
  };
  create_asset.descriptor_sha[0] = kCreateDescriptorSeed;

  auto replace_asset = AssetSpec {
    .key = replace_key,
    .asset_type = data::AssetType::kMaterial,
    .descriptor_relpath = "replace.desc",
    .virtual_path = "/Game/Patch/Replace.asset",
    .descriptor_size = kDescSize,
    .descriptor_sha = {},
  };
  replace_asset.descriptor_sha[0] = kReplaceDescriptorSeed;

  auto unchanged_asset = AssetSpec {
    .key = unchanged_key,
    .asset_type = data::AssetType::kScene,
    .descriptor_relpath = "unchanged.desc",
    .virtual_path = "/Game/Patch/Unchanged.asset",
    .descriptor_size = paktest::MakeEmptySceneDescriptor().size(),
    .descriptor_sha = {},
    .descriptor_payload = paktest::MakeEmptySceneDescriptor(),
  };
  unchanged_asset.descriptor_sha[0] = kUnchangedDescriptorSeed;

  const auto replace_files = std::array<FileSpec, 4> {
    FileSpec {
      .kind = lc::FileKind::kTexturesTable,
      .relpath = "Resources/textures.table",
      .payload = std::vector<std::byte>(sizeof(core::TextureResourceDesc)),
    },
    FileSpec {
      .kind = lc::FileKind::kTexturesData,
      .relpath = "Resources/textures.data",
      .payload = std::vector<std::byte>(kTexturePayloadBytes),
    },
    FileSpec {
      .kind = lc::FileKind::kBuffersTable,
      .relpath = "Resources/buffers.table",
      .payload = std::vector<std::byte>(sizeof(core::BufferResourceDesc)),
    },
    FileSpec {
      .kind = lc::FileKind::kBuffersData,
      .relpath = "Resources/buffers.data",
      .payload = std::vector<std::byte>(kBufferPayloadBytes),
    },
  };

  ASSERT_TRUE(paktest::WriteLooseIndex(source_create,
    std::span<const AssetSpec>(&create_asset, 1U), std::span<const FileSpec> {},
    kCreateGuidSeed));
  ASSERT_TRUE(paktest::WriteLooseIndex(source_replace,
    std::span<const AssetSpec>(&replace_asset, 1U),
    std::span<const FileSpec>(replace_files.data(), replace_files.size()),
    kReplaceGuidSeed));
  ASSERT_TRUE(paktest::WriteLooseIndex(source_unchanged,
    std::span<const AssetSpec>(&unchanged_asset, 1U),
    std::span<const FileSpec> {}, kUnchangedGuidSeed));

  const auto base_entries = std::array<data::PakCatalogEntry, 3> {
    MakeCatalogEntry(replace_key, data::AssetType::kMaterial,
      MakeDigest(kBaseReplaceDescriptorSeed),
      MakeDigest(kBaseReplaceDescriptorSeed)),
    MakeCatalogEntry(unchanged_key, data::AssetType::kScene,
      MakeDigest(kUnchangedDescriptorSeed),
      MakeDigest(kUnchangedDescriptorSeed)),
    MakeCatalogEntry(delete_key, data::AssetType::kScript,
      MakeDigest(kDeleteDescriptorSeed), EmptyDigest()),
  };
  const auto base_catalog
    = MakeBaseCatalog(std::span<const data::PakCatalogEntry>(
      base_entries.data(), base_entries.size()));

  const auto request_a = MakePatchRequest(Root() / "patch_a.pak",
    {
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked,
        .path = source_unchanged,
      },
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked,
        .path = source_create,
      },
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked,
        .path = source_replace,
      },
    },
    std::span<const data::PakCatalog>(&base_catalog, 1U));
  const auto request_b = MakePatchRequest(Root() / "patch_b.pak",
    {
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked,
        .path = source_replace,
      },
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked,
        .path = source_unchanged,
      },
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked,
        .path = source_create,
      },
    },
    std::span<const data::PakCatalog>(&base_catalog, 1U));

  const auto builder = pak::PakPlanBuilder {};
  const auto result_a = builder.Build(request_a);
  const auto result_b = builder.Build(request_b);

  ASSERT_FALSE(HasError(result_a.diagnostics));
  ASSERT_FALSE(HasError(result_b.diagnostics));
  ASSERT_TRUE(result_a.plan.has_value());
  ASSERT_TRUE(result_b.plan.has_value());

  EXPECT_EQ(
    PatchActionSignature(*result_a.plan), PatchActionSignature(*result_b.plan));

  const auto* create_action = FindAction(*result_a.plan, create_key);
  const auto* replace_action = FindAction(*result_a.plan, replace_key);
  const auto* unchanged_action = FindAction(*result_a.plan, unchanged_key);
  const auto* delete_action = FindAction(*result_a.plan, delete_key);
  ASSERT_NE(create_action, nullptr);
  ASSERT_NE(replace_action, nullptr);
  ASSERT_NE(unchanged_action, nullptr);
  ASSERT_NE(delete_action, nullptr);
  EXPECT_EQ(create_action->action, pak::PakPatchAction::kCreate);
  EXPECT_EQ(replace_action->action, pak::PakPatchAction::kReplace);
  EXPECT_EQ(unchanged_action->action, pak::PakPatchAction::kUnchanged);
  EXPECT_EQ(delete_action->action, pak::PakPatchAction::kDelete);

  EXPECT_TRUE(ContainsAsset(*result_a.plan, create_key));
  EXPECT_TRUE(ContainsAsset(*result_a.plan, replace_key));
  EXPECT_FALSE(ContainsAsset(*result_a.plan, unchanged_key));
  EXPECT_FALSE(ContainsAsset(*result_a.plan, delete_key));

  EXPECT_EQ(result_a.summary.patch_created, 1U);
  EXPECT_EQ(result_a.summary.patch_replaced, 1U);
  EXPECT_EQ(result_a.summary.patch_deleted, 1U);
  EXPECT_EQ(result_a.summary.patch_unchanged, 1U);
}

NOLINT_TEST_F(PakPatchPlannerTest, PatchModeUsesPatchLocalResourcesAndClosure)
{
  using data::CookedSource;
  using data::CookedSourceKind;

  constexpr auto kDescSize = uint64_t { 20U };
  constexpr auto kReplaceAssetSeed = uint8_t { 0x21U };
  constexpr auto kUnchangedAssetSeed = uint8_t { 0x22U };
  constexpr auto kReplaceDescriptorSeed = uint8_t { 0x71U };
  constexpr auto kBaseReplaceDescriptorSeed = uint8_t { 0x73U };
  constexpr auto kUnchangedDescriptorSeed = uint8_t { 0x72U };
  constexpr auto kReplaceTexturePayloadBytes = size_t { 23U };
  constexpr auto kUnchangedBufferPayloadBytes = size_t { 29U };
  constexpr auto kReplaceGuidSeed = uint8_t { 5U };
  constexpr auto kUnchangedGuidSeed = uint8_t { 6U };

  const auto source_replace = Root() / "a_replace";
  const auto source_unchanged = Root() / "b_unchanged";

  const auto replace_key = MakeAssetKey(kReplaceAssetSeed);
  const auto unchanged_key = MakeAssetKey(kUnchangedAssetSeed);

  auto replace_asset = AssetSpec {
    .key = replace_key,
    .asset_type = data::AssetType::kMaterial,
    .descriptor_relpath = "replace.desc",
    .virtual_path = "/Game/Patch/Replace2.asset",
    .descriptor_size = kDescSize,
    .descriptor_sha = {},
  };
  replace_asset.descriptor_sha[0] = kReplaceDescriptorSeed;

  auto unchanged_asset = AssetSpec {
    .key = unchanged_key,
    .asset_type = data::AssetType::kMaterial,
    .descriptor_relpath = "unchanged.desc",
    .virtual_path = "/Game/Patch/Unchanged2.asset",
    .descriptor_size = kDescSize,
    .descriptor_sha = {},
  };
  unchanged_asset.descriptor_sha[0] = kUnchangedDescriptorSeed;

  const auto replace_files = std::array<FileSpec, 2> {
    FileSpec {
      .kind = lc::FileKind::kTexturesTable,
      .relpath = "Resources/textures.table",
      .payload = std::vector<std::byte>(sizeof(core::TextureResourceDesc)),
    },
    FileSpec {
      .kind = lc::FileKind::kTexturesData,
      .relpath = "Resources/textures.data",
      .payload = std::vector<std::byte>(kReplaceTexturePayloadBytes),
    },
  };
  const auto unchanged_files = std::array<FileSpec, 2> {
    FileSpec {
      .kind = lc::FileKind::kBuffersTable,
      .relpath = "Resources/buffers.table",
      .payload = std::vector<std::byte>(sizeof(core::BufferResourceDesc)),
    },
    FileSpec {
      .kind = lc::FileKind::kBuffersData,
      .relpath = "Resources/buffers.data",
      .payload = std::vector<std::byte>(kUnchangedBufferPayloadBytes),
    },
  };

  ASSERT_TRUE(paktest::WriteLooseIndex(source_replace,
    std::span<const AssetSpec>(&replace_asset, 1U),
    std::span<const FileSpec>(replace_files.data(), replace_files.size()),
    kReplaceGuidSeed));
  ASSERT_TRUE(paktest::WriteLooseIndex(source_unchanged,
    std::span<const AssetSpec>(&unchanged_asset, 1U),
    std::span<const FileSpec>(unchanged_files.data(), unchanged_files.size()),
    kUnchangedGuidSeed));

  const auto base_entries = std::array<data::PakCatalogEntry, 2> {
    MakeCatalogEntry(replace_key, data::AssetType::kMaterial,
      MakeDigest(kBaseReplaceDescriptorSeed),
      MakeDigest(kBaseReplaceDescriptorSeed)),
    MakeCatalogEntry(unchanged_key, data::AssetType::kMaterial,
      MakeDigest(kUnchangedDescriptorSeed),
      MakeDigest(kUnchangedDescriptorSeed)),
  };
  const auto base_catalog
    = MakeBaseCatalog(std::span<const data::PakCatalogEntry>(
      base_entries.data(), base_entries.size()));
  const auto request = MakePatchRequest(Root() / "patch_local.pak",
    {
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked,
        .path = source_unchanged,
      },
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked,
        .path = source_replace,
      },
    },
    std::span<const data::PakCatalog>(&base_catalog, 1U));

  const auto result = pak::PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(result.diagnostics));
  ASSERT_TRUE(result.plan.has_value());

  const auto* replace_action = FindAction(*result.plan, replace_key);
  const auto* unchanged_action = FindAction(*result.plan, unchanged_key);
  ASSERT_NE(replace_action, nullptr);
  ASSERT_NE(unchanged_action, nullptr);
  EXPECT_EQ(replace_action->action, pak::PakPatchAction::kReplace);
  EXPECT_EQ(unchanged_action->action, pak::PakPatchAction::kUnchanged);

  EXPECT_TRUE(ContainsAsset(*result.plan, replace_key));
  EXPECT_FALSE(ContainsAsset(*result.plan, unchanged_key));
  ASSERT_EQ(result.plan->Resources().size(), 1U);
  EXPECT_EQ(result.plan->Resources()[0].resource_kind, "texture");

  const auto tables = result.plan->Tables();
  const auto texture_table_it
    = std::ranges::find_if(tables, [](const auto& table) -> auto {
        return table.table_name == "texture_table";
      });
  const auto buffer_table_it
    = std::ranges::find_if(tables, [](const auto& table) -> auto {
        return table.table_name == "buffer_table";
      });
  ASSERT_NE(texture_table_it, tables.end());
  ASSERT_NE(buffer_table_it, tables.end());
  EXPECT_EQ(texture_table_it->count, 1U);
  EXPECT_EQ(buffer_table_it->count, 0U);

  const auto closure = result.plan->PatchClosure();
  ASSERT_EQ(closure.size(), 1U);
  EXPECT_EQ(closure[0].asset_key, replace_key);
  EXPECT_EQ(closure[0].resource_kind, "texture");
  EXPECT_FALSE(HasDiagnosticCode(
    result.diagnostics, "pak.plan.stage.patch.closure_incomplete"));
}

NOLINT_TEST_F(PakPatchPlannerTest, ReplaceAssetTypeMismatchIsRejected)
{
  using data::CookedSource;
  using data::CookedSourceKind;

  constexpr auto kAssetSeed = uint8_t { 0x31U };
  constexpr auto kSourceDescriptorSeed = uint8_t { 0x55U };
  constexpr auto kBaseDescriptorSeed = uint8_t { 0x12U };
  constexpr auto kGuidSeed = uint8_t { 11U };

  const auto source = Root() / "type_mismatch";
  const auto key = MakeAssetKey(kAssetSeed);

  auto source_asset = AssetSpec {
    .key = key,
    .asset_type = data::AssetType::kScene,
    .descriptor_relpath = "scene.desc",
    .virtual_path = "/Game/Patch/TypeMismatch.asset",
    .descriptor_size = paktest::MakeEmptySceneDescriptor().size(),
    .descriptor_sha = {},
    .descriptor_payload = paktest::MakeEmptySceneDescriptor(),
  };
  source_asset.descriptor_sha[0] = kSourceDescriptorSeed;
  ASSERT_TRUE(paktest::WriteLooseIndex(source,
    std::span<const AssetSpec>(&source_asset, 1U), std::span<const FileSpec> {},
    kGuidSeed));

  const auto base_entries = std::array<data::PakCatalogEntry, 1> {
    MakeCatalogEntry(key, data::AssetType::kMaterial,
      MakeDigest(kBaseDescriptorSeed), EmptyDigest()),
  };
  const auto base_catalog
    = MakeBaseCatalog(std::span<const data::PakCatalogEntry>(
      base_entries.data(), base_entries.size()));
  const auto request = MakePatchRequest(Root() / "type_mismatch.pak",
    { CookedSource { .kind = CookedSourceKind::kLooseCooked, .path = source } },
    std::span<const data::PakCatalog>(&base_catalog, 1U));

  const auto result = pak::PakPlanBuilder {}.Build(request);
  ASSERT_TRUE(HasError(result.diagnostics));
  EXPECT_FALSE(result.plan.has_value());
  EXPECT_TRUE(
    HasDiagnosticCode(result.diagnostics, "pak.plan.replace_type_mismatch"));
}

NOLINT_TEST_F(PakPatchPlannerTest,
  PatchModeOnlyReplacesChangedAssetWithinSingleLooseCookedSource)
{
  using data::CookedSource;
  using data::CookedSourceKind;

  const auto patch_source = Root() / "single_source_patch";

  const auto asset_a_key = MakeAssetKey(0x61U);
  const auto asset_b_key = MakeAssetKey(0x62U);

  const auto material_a_bytes = BuildMaterialDescriptor("MatA");
  const auto material_b_bytes = BuildMaterialDescriptor("MatB");

  const auto material_a_sha
    = ToLooseSha(base::ComputeSha256(std::span<const std::byte>(
      material_a_bytes.data(), material_a_bytes.size())));
  const auto material_b_sha
    = ToLooseSha(base::ComputeSha256(std::span<const std::byte>(
      material_b_bytes.data(), material_b_bytes.size())));

  const auto texture_fallback_payload
    = std::vector<std::byte>(8U, std::byte { 0x01 });
  const auto texture_a_payload = std::vector<std::byte>(8U, std::byte { 0x11 });
  const auto texture_b_payload_base
    = std::vector<std::byte>(8U, std::byte { 0x22 });
  auto texture_b_payload_patch = std::vector<std::byte>(8U, std::byte { 0x22 });
  texture_b_payload_patch[0] = std::byte { 0x33 };

  const auto texture_record_fallback = BuildTextureRecord(
    0U, static_cast<uint32_t>(texture_fallback_payload.size()));
  const auto texture_record_a
    = BuildTextureRecord(texture_fallback_payload.size(),
      static_cast<uint32_t>(texture_a_payload.size()));
  const auto texture_record_b = BuildTextureRecord(
    texture_fallback_payload.size() + texture_a_payload.size(),
    static_cast<uint32_t>(texture_b_payload_base.size()));

  auto textures_table_bytes
    = std::vector<std::byte>(3U * sizeof(core::TextureResourceDesc));
  std::memcpy(textures_table_bytes.data(), &texture_record_fallback,
    sizeof(texture_record_fallback));
  std::memcpy(textures_table_bytes.data() + sizeof(texture_record_fallback),
    &texture_record_a, sizeof(texture_record_a));
  std::memcpy(textures_table_bytes.data() + sizeof(texture_record_fallback)
      + sizeof(texture_record_a),
    &texture_record_b, sizeof(texture_record_b));

  auto textures_data_base = texture_fallback_payload;
  textures_data_base.insert(textures_data_base.end(), texture_a_payload.begin(),
    texture_a_payload.end());
  textures_data_base.insert(textures_data_base.end(),
    texture_b_payload_base.begin(), texture_b_payload_base.end());

  auto textures_data_patch = texture_fallback_payload;
  textures_data_patch.insert(textures_data_patch.end(),
    texture_a_payload.begin(), texture_a_payload.end());
  textures_data_patch.insert(textures_data_patch.end(),
    texture_b_payload_patch.begin(), texture_b_payload_patch.end());

  const auto assets = std::array<AssetSpec, 2> {
    AssetSpec {
      .key = asset_a_key,
      .asset_type = data::AssetType::kMaterial,
      .descriptor_relpath = "Materials/MatA.omat",
      .virtual_path = "/Game/MatA.omat",
      .descriptor_size = material_a_bytes.size(),
      .descriptor_sha = material_a_sha,
      .descriptor_payload = material_a_bytes,
      .references = data::AssetReferences::Create(
        {
          { .kind = data::ResourceKind::kTexture,
            .index = oxygen::ResourceIndexT { 1U } },
        },
        {})
        .value(),
    },
    AssetSpec {
      .key = asset_b_key,
      .asset_type = data::AssetType::kMaterial,
      .descriptor_relpath = "Materials/MatB.omat",
      .virtual_path = "/Game/MatB.omat",
      .descriptor_size = material_b_bytes.size(),
      .descriptor_sha = material_b_sha,
      .descriptor_payload = material_b_bytes,
      .references = data::AssetReferences::Create(
        {
          { .kind = data::ResourceKind::kTexture,
            .index = oxygen::ResourceIndexT { 2U } },
        },
        {})
        .value(),
    },
  };

  const auto base_files = std::array<FileSpec, 2> {
    FileSpec {
      .kind = lc::FileKind::kTexturesTable,
      .relpath = "Resources/textures.table",
      .payload = textures_table_bytes,
    },
    FileSpec {
      .kind = lc::FileKind::kTexturesData,
      .relpath = "Resources/textures.data",
      .payload = textures_data_base,
    },
  };
  const auto patch_files = std::array<FileSpec, 2> {
    FileSpec {
      .kind = lc::FileKind::kTexturesTable,
      .relpath = "Resources/textures.table",
      .payload = textures_table_bytes,
    },
    FileSpec {
      .kind = lc::FileKind::kTexturesData,
      .relpath = "Resources/textures.data",
      .payload = textures_data_patch,
    },
  };

  ASSERT_TRUE(paktest::WriteLooseIndex(patch_source,
    std::span<const AssetSpec>(assets.data(), assets.size()),
    std::span<const FileSpec>(patch_files.data(), patch_files.size()), 0x92U));
  const auto asset_a_transitive = AggregateTaggedDigests(
    std::array<std::pair<uint16_t, base::Sha256Digest>, 1> {
      std::pair<uint16_t, base::Sha256Digest> { 0x0002U,
        ComputeNormalizedTextureDigest(texture_record_a,
          std::span<const std::byte>(
            texture_a_payload.data(), texture_a_payload.size())) },
    });
  const auto asset_b_base_transitive = AggregateTaggedDigests(
    std::array<std::pair<uint16_t, base::Sha256Digest>, 1> {
      std::pair<uint16_t, base::Sha256Digest> { 0x0002U,
        ComputeNormalizedTextureDigest(texture_record_b,
          std::span<const std::byte>(
            texture_b_payload_base.data(), texture_b_payload_base.size())) },
    });
  const auto base_entries = std::array<data::PakCatalogEntry, 2> {
    MakeCatalogEntry(asset_a_key, data::AssetType::kMaterial,
      base::ComputeSha256(std::span<const std::byte>(
        material_a_bytes.data(), material_a_bytes.size())),
      asset_a_transitive),
    MakeCatalogEntry(asset_b_key, data::AssetType::kMaterial,
      base::ComputeSha256(std::span<const std::byte>(
        material_b_bytes.data(), material_b_bytes.size())),
      asset_b_base_transitive),
  };
  const auto base_catalog
    = MakeBaseCatalog(std::span<const data::PakCatalogEntry>(
      base_entries.data(), base_entries.size()));

  const auto builder = pak::PakPlanBuilder {};
  const auto patch_request = MakePatchRequest(Root() / "single_patch.pak",
    {
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked,
        .path = patch_source,
      },
    },
    std::span<const data::PakCatalog>(&base_catalog, 1U));

  const auto patch_result = builder.Build(patch_request);
  ASSERT_FALSE(HasError(patch_result.diagnostics))
    << DiagnosticSummary(patch_result.diagnostics);
  ASSERT_TRUE(patch_result.plan.has_value());

  const auto* action_a = FindAction(*patch_result.plan, asset_a_key);
  const auto* action_b = FindAction(*patch_result.plan, asset_b_key);
  ASSERT_NE(action_a, nullptr);
  ASSERT_NE(action_b, nullptr);
  EXPECT_EQ(action_a->action, pak::PakPatchAction::kUnchanged);
  EXPECT_EQ(action_b->action, pak::PakPatchAction::kReplace);

  EXPECT_FALSE(ContainsAsset(*patch_result.plan, asset_a_key));
  EXPECT_TRUE(ContainsAsset(*patch_result.plan, asset_b_key));
  ASSERT_EQ(patch_result.plan->Resources().size(), 1U);
  EXPECT_EQ(patch_result.plan->Resources()[0].resource_kind, "texture");

  const auto closure = patch_result.plan->PatchClosure();
  ASSERT_EQ(closure.size(), 1U);
  EXPECT_EQ(closure[0].asset_key, asset_b_key);
  EXPECT_EQ(closure[0].resource_kind, "texture");

  EXPECT_EQ(patch_result.summary.patch_created, 0U);
  EXPECT_EQ(patch_result.summary.patch_replaced, 1U);
  EXPECT_EQ(patch_result.summary.patch_deleted, 0U);
  EXPECT_EQ(patch_result.summary.patch_unchanged, 1U);
}

NOLINT_TEST_F(PakPatchPlannerTest, PatchPreservesSceneLocalScriptDescriptor)
{
  const auto source = Root() / "scene";
  const auto assets = MakeSceneScriptAssets(1U);
  ASSERT_TRUE(paktest::WriteLooseIndex(source, assets, {}, 1U));
  const auto base = MakeBaseCatalog({});
  const auto request = MakePatchRequest(Root() / "scene-patch.pak",
    { { .kind = data::CookedSourceKind::kLooseCooked, .path = source } },
    std::span(&base, 1U));
  const auto result = pak::PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(result.diagnostics))
    << DiagnosticSummary(result.diagnostics);
  ASSERT_TRUE(result.plan.has_value());
  ASSERT_FALSE(
    HasError(pak::PakWriter {}.Write(request, *result.plan).diagnostics));
  auto archive = oxygen::content::PakFile(request.output_pak_path);
  const auto entry = archive.FindEntry(MakeAssetKey(1U));
  ASSERT_TRUE(entry.has_value());
  auto reader = archive.CreateReader(*entry);
  const auto bytes = reader.ReadBlob(entry->desc_size);
  ASSERT_TRUE(bytes.has_value());
  EXPECT_EQ(*bytes, BuildSceneDescriptorWithScriptingSlots(1U));
}

NOLINT_TEST_F(
  PakPatchPlannerTest, ScriptBindingsSurviveSourceMergeAndPakRepacking)
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
  auto request = pak::PakBuildRequest {
    .mode = pak::BuildMode::kFull,
    .sources = sources,
    .output_pak_path = Root() / "scripts.pak",
    .output_manifest_path = {},
    .content_version = 1U,
    .source_key = MakeSourceKey(3U),
    .base_catalogs = {},

    .options = {},
  };
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
      << DiagnosticSummary(planned.diagnostics);
    ASSERT_TRUE(planned.plan.has_value());
    const auto written = pak::PakWriter {}.Write(build_request, *planned.plan);
    ASSERT_FALSE(HasError(written.diagnostics))
      << DiagnosticSummary(written.diagnostics);
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

NOLINT_TEST_F(PakPatchPlannerTest, BufferAndScriptReferencesSurvivePakRepacking)
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
        .descriptor_sha = ToLooseSha(base::ComputeSha256(geometry_bytes)),
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
        .descriptor_sha = ToLooseSha(base::ComputeSha256(script_bytes)),
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
  auto request = pak::PakBuildRequest {
    .mode = pak::BuildMode::kFull,
    .sources = sources,
    .output_pak_path = Root() / "resources.pak",
    .output_manifest_path = {},
    .content_version = 1U,
    .source_key = MakeSourceKey(3U),
    .base_catalogs = {},

    .options = {},
  };
  const auto initial = pak::PakPlanBuilder {}.Build(request);
  ASSERT_TRUE(initial.plan) << DiagnosticSummary(initial.diagnostics);
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
    ASSERT_TRUE(planned.plan) << DiagnosticSummary(planned.diagnostics);
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
