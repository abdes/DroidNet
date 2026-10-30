//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/PakPlanBuilder.cpp (resource compaction)

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "PakTestSupport.h"
#include "PakWriterTestSupport.h"

#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Cooker/Import/Internal/Utils/BufferDescriptorSidecar.h>
#include <Oxygen/Cooker/Pak/PakBuildReport.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakPlan.h>
#include <Oxygen/Cooker/Pak/PakPlanBuilder.h>
#include <Oxygen/Cooker/Pak/PakWriter.h>
#include <Oxygen/Cooker/test/Support/DescriptorFixtures.h>
#include <Oxygen/Cooker/test/Support/TestValues.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/TextureResourceDescriptor.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace content = oxygen::content;
namespace core = oxygen::data::pak::core;
namespace data = oxygen::data;
namespace fixtures = oxygen::content::test;
namespace lc = oxygen::data::loose_cooked;
namespace pak = oxygen::content::pak;
namespace paktest = oxygen::content::pak::test;

using paktest::AssetSpec;
using paktest::FileSpec;
using paktest::HasError;
using paktest::MakeAssetKey;
using paktest::MakePatternBytes;

constexpr auto kResourceBytes = uint32_t { 4U };
constexpr auto kDroppedCode = std::string_view {
  "pak.plan.unreferenced_resources_dropped",
};
constexpr auto kSidecarBufferCode = std::string_view {
  "pak.plan.sidecar_buffer_dropped",
};

//! A loose cooked root: resource rows past the sentinel row zero, their texture
//! and buffer sidecars, and its assets.
struct LooseRoot final {
  //! Pattern seed of each texture row past row zero, so equal seeds are equal
  //! textures.
  std::vector<uint8_t> textures;
  std::vector<uint8_t> buffers;
  std::vector<FileSpec> sidecars;
  std::vector<AssetSpec> assets;
};

//! A texture table row and the pattern seed of its texture.
struct TextureRow final {
  uint32_t index = 0U;
  uint8_t seed = 0U;
};

auto TextureRecord(const TextureRow& row) -> core::TextureResourceDesc
{
  auto record = core::TextureResourceDesc {};
  record.data_offset = static_cast<uint64_t>(row.index - 1U) * kResourceBytes;
  record.size_bytes = kResourceBytes;
  record.width = row.seed;
  record.height = 1U;
  record.depth = 1U;
  record.array_layers = 1U;
  record.mip_levels = 1U;
  return record;
}

auto BufferRecord(const uint32_t row) -> core::BufferResourceDesc
{
  auto record = core::BufferResourceDesc {};
  record.data_offset = static_cast<uint64_t>(row - 1U) * kResourceBytes;
  record.size_bytes = kResourceBytes;
  record.element_stride = 1U;
  return record;
}

template <typename Record>
auto TableBytes(const std::vector<Record>& records) -> std::vector<std::byte>
{
  const auto bytes = std::as_bytes(std::span { records });
  return { bytes.begin(), bytes.end() };
}

auto DataBytes(const std::vector<uint8_t>& seeds) -> std::vector<std::byte>
{
  auto bytes = std::vector<std::byte> {};
  for (const auto seed : seeds) {
    const auto row = MakePatternBytes(std::byte { seed }, kResourceBytes);
    bytes.insert(bytes.end(), row.begin(), row.end());
  }
  return bytes;
}

auto WriteRoot(const std::filesystem::path& root, const LooseRoot& spec,
  const uint8_t guid_seed) -> void
{
  auto textures = std::vector<core::TextureResourceDesc>(1U);
  for (uint32_t row = 1U; row <= spec.textures.size(); ++row) {
    textures.push_back(
      TextureRecord({ .index = row, .seed = spec.textures.at(row - 1U) }));
  }
  auto buffers = std::vector<core::BufferResourceDesc>(1U);
  for (uint32_t row = 1U; row <= spec.buffers.size(); ++row) {
    buffers.push_back(BufferRecord(row));
  }
  auto files = std::vector<FileSpec> {
    {
      .kind = lc::FileKind::kTexturesTable,
      .relpath = "Resources/textures.table",
      .payload = TableBytes(textures),
    },
    {
      .kind = lc::FileKind::kTexturesData,
      .relpath = "Resources/textures.data",
      .payload = DataBytes(spec.textures),
    },
    {
      .kind = lc::FileKind::kBuffersTable,
      .relpath = "Resources/buffers.table",
      .payload = TableBytes(buffers),
    },
    {
      .kind = lc::FileKind::kBuffersData,
      .relpath = "Resources/buffers.data",
      .payload = DataBytes(spec.buffers),
    },
  };
  files.insert(files.end(), spec.sidecars.begin(), spec.sidecars.end());
  ASSERT_TRUE(paktest::WriteLooseIndex(root, spec.assets, files, guid_seed));
}

//! A texture descriptor naming texture row `row` under `virtual_path`.
auto TextureSidecar(const std::string& relpath, const std::string& virtual_path,
  const TextureRow& row) -> FileSpec
{
  return FileSpec {
    .kind = lc::FileKind::kTextureDescriptor,
    .relpath = relpath,
    .payload = data::EncodeTextureResourceDescriptor({
      .index = core::ResourceIndexT { row.index },
      .descriptor = TextureRecord(row),
    }),
    .virtual_path = virtual_path,
  };
}

//! A `.obuf` buffer sidecar naming buffer `row`.
auto BufferSidecar(const std::string& relpath, const uint32_t row) -> FileSpec
{
  return FileSpec {
    .kind = lc::FileKind::kAuxiliary,
    .relpath = relpath,
    .payload = content::import::internal::SerializeBufferDescriptorSidecar(
      core::ResourceIndexT { row }, BufferRecord(row), {}),
  };
}

auto Bindings(const data::ResourceKind kind, const uint32_t index)
  -> data::AssetReferences
{
  return data::AssetReferences::Create(
    {
      data::ResourceBinding {
        .kind = kind,
        .index = core::ResourceIndexT { index },
      },
    },
    {})
    .value();
}

auto Material(const uint8_t seed, const std::string& name,
  data::AssetReferences references) -> AssetSpec
{
  auto bytes
    = fixtures::MaterialDescriptor(name, fixtures::MaterialVariant::kPlain)
        .bytes;
  return AssetSpec {
    .key = MakeAssetKey(seed),
    .asset_type = data::AssetType::kMaterial,
    .descriptor_relpath = "Materials/" + name + ".omat",
    .virtual_path = "/Content/Materials/" + name + ".omat",
    .descriptor_size = bytes.size(),
    .descriptor_sha = {},
    .descriptor_payload = std::move(bytes),
    .references = std::move(references),
  };
}

auto Geometry(const uint8_t seed, const std::string& name,
  const uint32_t buffer) -> AssetSpec
{
  auto bytes = MakePatternBytes(std::byte { seed }, 64U);
  return AssetSpec {
    .key = MakeAssetKey(seed),
    .asset_type = data::AssetType::kGeometry,
    .descriptor_relpath = "Geometry/" + name + ".ogeo",
    .virtual_path = "/Content/Geometry/" + name + ".ogeo",
    .descriptor_size = bytes.size(),
    .descriptor_sha = {},
    .descriptor_payload = std::move(bytes),
    .references = Bindings(data::ResourceKind::kBuffer, buffer),
  };
}

auto LooseSource(const std::filesystem::path& root) -> data::CookedSource
{
  return { .kind = data::CookedSourceKind::kLooseCooked, .path = root };
}

auto TableCount(const pak::PakPlan& plan, const std::string_view name)
  -> uint32_t
{
  const auto tables = plan.Tables();
  const auto table
    = std::ranges::find(tables, name, &pak::PakTablePlan::table_name);
  return table == tables.end() ? 0U : table->count;
}

auto Diagnostics(const std::vector<pak::PakDiagnostic>& diagnostics,
  const std::string_view code) -> std::vector<pak::PakDiagnostic>
{
  auto found = std::vector<pak::PakDiagnostic> {};
  std::ranges::copy_if(diagnostics, std::back_inserter(found),
    [code](const pak::PakDiagnostic& diagnostic) -> bool {
      return diagnostic.code == code;
    });
  return found;
}

auto BoundIndex(const content::PakFile& pak_file, const data::AssetKey& key)
  -> uint32_t
{
  const auto references = pak_file.ReadAssetReferences(key);
  return references.Resources().front().index.get();
}

class PakCompactionTest : public paktest::TempDirFixture { };

//! A full pak of a root holding entries of earlier cooks packs only the
//! resources its assets bind, renumbers them, and reports the dropped entries
//! as one information per kind.
NOLINT_TEST_F(PakCompactionTest, FullPakDropsStaleEntriesAndRemapsBindings)
{
  // Arrange
  const auto root = Root() / "stale";
  const auto output = Root() / "stale.pak";
  const auto wood
    = Material(0x21U, "Wood", Bindings(data::ResourceKind::kTexture, 2U));
  const auto crate = Geometry(0x22U, "Crate", 2U);
  WriteRoot(root,
    {
      .textures = { 0x30U, 0x31U },
      .buffers = { 0x40U, 0x41U },
      .sidecars = {},
      .assets = { wood, crate },
    },
    0x10U);
  const auto request
    = paktest::MakeFullRequest(output, { .sources = { LooseSource(root) } });

  // Act
  const auto planned = pak::PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(planned.diagnostics));
  ASSERT_HAS_VALUE(planned.plan);
  const auto written = pak::PakWriter {}.Write(request, *planned.plan);
  ASSERT_FALSE(HasError(written.diagnostics));
  const auto pak_file = content::PakFile(output);

  // Assert
  EXPECT_EQ(TableCount(*planned.plan, "texture_table"), 2U);
  EXPECT_EQ(TableCount(*planned.plan, "buffer_table"), 2U);
  EXPECT_EQ(BoundIndex(pak_file, wood.key), 1U);
  EXPECT_EQ(BoundIndex(pak_file, crate.key), 1U);
  const auto dropped = Diagnostics(planned.diagnostics, kDroppedCode);
  ASSERT_EQ(dropped.size(), 2U);
  for (const auto& diagnostic : dropped) {
    EXPECT_EQ(diagnostic.severity, pak::PakDiagnosticSeverity::kInfo);
    EXPECT_EQ(diagnostic.path.filename(), root.filename());
    EXPECT_THAT(diagnostic.message, ::testing::HasSubstr("Dropped 1 "));
  }
  EXPECT_TRUE(Diagnostics(planned.diagnostics, kSidecarBufferCode).empty());
}

//! A texture re-cooked under the same name leaves its old entry behind; the
//! pak packs only the entry the name now resolves to.
NOLINT_TEST_F(
  PakCompactionTest, TextureRecookedUnderTheSameNameKeepsTheNewEntry)
{
  // Arrange
  const auto root = Root() / "same_name";
  const auto output = Root() / "same_name.pak";
  WriteRoot(root,
    {
      .textures = { 0x30U, 0x31U },
      .buffers = {},
      .sidecars = { TextureSidecar(
        "Textures/Sky.otex", "/Content/Textures/Sky.otex", { .index = 2U, .seed = 0x31U }), },
      .assets = {},
    },
    0x11U);
  const auto request
    = paktest::MakeFullRequest(output, { .sources = { LooseSource(root) } });

  // Act
  const auto planned = pak::PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(planned.diagnostics));
  ASSERT_HAS_VALUE(planned.plan);
  const auto written = pak::PakWriter {}.Write(request, *planned.plan);
  ASSERT_FALSE(HasError(written.diagnostics));
  const auto pak_file = content::PakFile(output);

  // Assert
  EXPECT_EQ(TableCount(*planned.plan, "texture_table"), 2U);
  EXPECT_EQ(
    pak_file.ResolveTextureIndexByVirtualPath("/Content/Textures/Sky.otex"),
    core::ResourceIndexT { 1U });
  const auto dropped = Diagnostics(planned.diagnostics, kDroppedCode);
  ASSERT_EQ(dropped.size(), 1U);
  EXPECT_THAT(
    dropped.front().message, ::testing::HasSubstr("1 unreferenced texture"));
}

//! A texture re-cooked under a new name retires its old descriptor, so the old
//! entry is unreachable and the pak drops it.
NOLINT_TEST_F(PakCompactionTest, TextureRecookedUnderANewNameDropsTheOldEntry)
{
  // Arrange
  const auto root = Root() / "new_name";
  const auto output = Root() / "new_name.pak";
  WriteRoot(root,
    {
      .textures = { 0x30U, 0x31U },
      .buffers = {},
      .sidecars = { TextureSidecar(
        "Textures/Night.otex", "/Content/Textures/Night.otex", { .index = 2U, .seed = 0x31U }), },
      .assets = {},
    },
    0x12U);
  const auto request
    = paktest::MakeFullRequest(output, { .sources = { LooseSource(root) } });

  // Act
  const auto planned = pak::PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(planned.diagnostics));
  ASSERT_HAS_VALUE(planned.plan);
  const auto written = pak::PakWriter {}.Write(request, *planned.plan);
  ASSERT_FALSE(HasError(written.diagnostics));
  const auto pak_file = content::PakFile(output);

  // Assert
  EXPECT_EQ(TableCount(*planned.plan, "texture_table"), 2U);
  EXPECT_EQ(
    pak_file.ResolveTextureIndexByVirtualPath("/Content/Textures/Night.otex"),
    core::ResourceIndexT { 1U });
  EXPECT_FALSE(
    pak_file.ResolveTextureIndexByVirtualPath("/Content/Textures/Sky.otex"));
  EXPECT_EQ(Diagnostics(planned.diagnostics, kDroppedCode).size(), 1U);
}

//! A buffer a geometry binds is packed; one only a live `.obuf` sidecar names
//! is dropped with a warning that gives the sidecar's path.
NOLINT_TEST_F(PakCompactionTest, BufferWithoutGeometryWarnsWithItsSidecar)
{
  // Arrange
  const auto root = Root() / "buffers";
  const auto output = Root() / "buffers.pak";
  const auto crate = Geometry(0x23U, "Crate", 1U);
  WriteRoot(root,
    {
      .textures = {},
      .buffers = { 0x40U, 0x41U },
      .sidecars = { BufferSidecar("Buffers/Crate.obuf", 1U),
        BufferSidecar("Buffers/Orphan.obuf", 2U), },
      .assets = { crate },
    },
    0x13U);
  const auto request
    = paktest::MakeFullRequest(output, { .sources = { LooseSource(root) } });

  // Act
  const auto planned = pak::PakPlanBuilder {}.Build(request);

  // Assert
  ASSERT_FALSE(HasError(planned.diagnostics));
  ASSERT_HAS_VALUE(planned.plan);
  EXPECT_EQ(TableCount(*planned.plan, "buffer_table"), 2U);
  const auto warnings = Diagnostics(planned.diagnostics, kSidecarBufferCode);
  ASSERT_EQ(warnings.size(), 1U);
  EXPECT_EQ(warnings.front().severity, pak::PakDiagnosticSeverity::kWarning);
  EXPECT_EQ(warnings.front().path.filename(), "Orphan.obuf");
  EXPECT_TRUE(Diagnostics(planned.diagnostics, kDroppedCode).empty());
}

//! A patch over a compacted base packs only the resources its changed assets
//! bind: a resource an unchanged asset binds, and a keyed texture the base
//! holds, stay with the base.
NOLINT_TEST_F(
  PakCompactionTest, PatchOverCompactedBasePacksOnlyChangedResources)
{
  // Arrange
  const auto sky = TextureSidecar("Textures/Sky.otex",
    "/Content/Textures/Sky.otex", { .index = 3U, .seed = 0x32U });
  const auto crate = Geometry(0x24U, "Crate", 1U);
  const auto base_root = Root() / "base";
  const auto base_output = Root() / "base.pak";
  WriteRoot(base_root,
    {
      .textures = { 0x30U, 0x31U, 0x32U },
      .buffers = { 0x40U },
      .sidecars = { sky },
      .assets
      = { Material(0x25U, "Wood", Bindings(data::ResourceKind::kTexture, 2U)),
        crate, },
    },
    0x14U);
  const auto base_request = paktest::MakeFullRequest(
    base_output, { .sources = { LooseSource(base_root) } });
  const auto base_plan = pak::PakPlanBuilder {}.Build(base_request);
  ASSERT_FALSE(HasError(base_plan.diagnostics));
  ASSERT_HAS_VALUE(base_plan.plan);
  ASSERT_FALSE(HasError(
    pak::PakWriter {}.Write(base_request, *base_plan.plan).diagnostics));
  ASSERT_EQ(TableCount(*base_plan.plan, "texture_table"), 3U);

  const auto recooked_root = Root() / "recooked";
  const auto patch_output = Root() / "patch.pak";
  const auto wood
    = Material(0x25U, "Wood", Bindings(data::ResourceKind::kTexture, 4U));
  WriteRoot(recooked_root,
    {
      .textures = { 0x30U, 0x31U, 0x32U, 0x33U },
      .buffers = { 0x40U },
      .sidecars = { sky },
      .assets = { wood, crate },
    },
    0x15U);
  auto request = paktest::MakePatchRequest(patch_output,
    { content::PakFile(base_output).Catalog() },
    {
      .sources = { LooseSource(recooked_root) },
      .source_key = paktest::MakeSourceKey(0x7EU),
    });
  request.base_texture_keys
    = { data::TextureKeyFromVirtualPath("/Content/Textures/Sky.otex") };

  // Act
  const auto planned = pak::PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(planned.diagnostics));
  ASSERT_HAS_VALUE(planned.plan);
  const auto written = pak::PakWriter {}.Write(request, *planned.plan);
  ASSERT_FALSE(HasError(written.diagnostics));
  const auto patch = content::PakFile(patch_output);

  // Assert
  const auto actions = planned.plan->PatchActions();
  const auto action_of = [&](const data::AssetKey& key) -> pak::PakPatchAction {
    return std::ranges::find(
      actions, key, &pak::PakPatchActionRecord::asset_key)
      ->action;
  };
  EXPECT_EQ(action_of(wood.key), pak::PakPatchAction::kReplace);
  EXPECT_EQ(action_of(crate.key), pak::PakPatchAction::kUnchanged);
  EXPECT_EQ(TableCount(*planned.plan, "texture_table"), 2U);
  EXPECT_EQ(TableCount(*planned.plan, "buffer_table"), 0U);
  EXPECT_EQ(BoundIndex(patch, wood.key), 1U);
  const auto dropped = Diagnostics(planned.diagnostics, kDroppedCode);
  ASSERT_EQ(dropped.size(), 1U);
  EXPECT_THAT(dropped.front().message, ::testing::HasSubstr("Dropped 2 "));
}

} // namespace
