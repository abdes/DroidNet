//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Cooked/CookedContent.cpp

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/testing/CookedBytes.h>
#include <Oxygen/Cooker/testing/CookedPak.h>
#include <Oxygen/Cooker/testing/GeometryBuilder.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Cooker/testing/PakWorkspace.h>
#include <Oxygen/Cooker/testing/ScriptBuilder.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/ResourceDescriptor.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace cooked = oxygen::content::cooked;
namespace core = oxygen::data::pak::core;
namespace data = oxygen::data;
namespace fixtures = oxygen::cooker::testing;
namespace fs = std::filesystem;
namespace lc = oxygen::data::loose_cooked;

using cooked::CookedContent;
using cooked::ReadCookedSummary;
using oxygen::ResourceIndexT;
using oxygen::content::lc::IntegrityCheck;
using oxygen::cooker::testing::CookedBytes;
using oxygen::cooker::testing::CookedPak;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::MemoryFileSystem;
using oxygen::cooker::testing::PakWorkspace;
using oxygen::cooker::testing::ScriptBuilder;
using oxygen::cooker::testing::SeedBytes;

constexpr auto kWoodPath = std::string_view { "/Game/Materials/Wood.omat" };
constexpr auto kCratePath = std::string_view { "/Game/Geometry/Crate.ogeo" };

//=== Builders ===------------------------------------------------------------//

//! The loose library every agreement test reads, as a loose root and as a pak:
//! a textured material, a geometry bound to a buffer, and a named texture.
struct Sample final {
  LooseCookedRoot library { "Game" };
  data::AssetKey material;
  data::AssetKey geometry;
  std::string sky;
  ResourceIndexT texture_row { 0U };
  ResourceIndexT buffer_row { 0U };
  std::vector<std::byte> texture_payload = SeedBytes(1U);
  std::vector<std::byte> buffer_payload = SeedBytes(2U);
  std::vector<std::byte> material_descriptor;
  std::vector<std::byte> geometry_descriptor;
};

auto MakeSample() -> Sample
{
  auto sample = Sample {};
  auto& library = sample.library;
  sample.texture_row = library.AddTexture(sample.texture_payload);
  sample.buffer_row = library.AddBuffer(sample.buffer_payload);
  sample.sky = library.DescribeTexture("Textures/Sky.otex", sample.texture_row);

  auto wood = fixtures::MaterialBuilder { "Wood" };
  wood.Texture(&data::pak::render::MaterialAssetDesc::base_color_texture,
    sample.texture_row);
  sample.material_descriptor = wood.Bytes();
  sample.material = library.Add({
    .type = data::AssetType::kMaterial,
    .leaf = "Materials/Wood.omat",
    .descriptor = sample.material_descriptor,
    .references = wood.References(),
  });

  sample.geometry_descriptor = fixtures::GeometryBuilder { "Crate" }.Bytes();
  sample.geometry = library.Add({
    .type = data::AssetType::kGeometry,
    .leaf = "Geometry/Crate.ogeo",
    .descriptor = sample.geometry_descriptor,
    .references = data::AssetReferences::Create(
      { { .kind = data::ResourceKind::kBuffer, .index = sample.buffer_row } },
      {})
      .value(),
  });
  return sample;
}

//! The files of a loose root that holds materials of two mounts and a texture
//! descriptor of one of them.
auto MixedMountFiles() -> fixtures::LooseCookedFiles
{
  const auto bytes = std::vector<std::byte>(8);
  auto root = LooseCookedRoot { "Props" };
  for (const auto* leaf : { "Materials/Red.omat", "Crate.omat" }) {
    static_cast<void>(root.Add({
      .type = data::AssetType::kMaterial,
      .leaf = leaf,
      .descriptor = bytes,
    }));
  }
  static_cast<void>(root.Add({
    .type = data::AssetType::kMaterial,
    .leaf = "Car.omat",
    .mount = "Vehicles",
    .descriptor = bytes,
  }));
  root.AddFile(lc::FileKind::kTextureDescriptor, "Props/Wood.otex", bytes,
    "/Props/Wood.otex");
  return root.Files();
}

//! The bytes of a valid pak of one material, which a defect then damages.
auto ValidPakBytes() -> std::vector<std::byte>
{
  auto library = LooseCookedRoot { "Game" };
  library.Material("Materials/Wood.omat");
  return CookedPak { std::move(library) }.Encode();
}

//! The kinds of cooked source a test opens the same content as.
enum class Flavor : uint8_t {
  kLoose,
  kPak,
};

auto FlavorName(const ::testing::TestParamInfo<Flavor>& info) -> std::string
{
  return std::string { info.param == Flavor::kLoose ? "Loose" : "Pak" };
}

//! The rule `CookedContent::Open` of `path` reports broken, if it fails.
auto OpenDefect(const cooked::CookerStorageReader& storage,
  const fs::path& path) -> std::optional<data::CookedDefect>
{
  try {
    static_cast<void>(CookedContent::Open(storage, path));
  } catch (const data::CookedFormatError& error) {
    return error.Defect();
  }
  return std::nullopt;
}

//! The defect `VerifyStorage` reports for `content`, if it fails.
auto VerifyDefect(const CookedContent& content, const IntegrityCheck check)
  -> std::optional<data::CookedDefect>
{
  try {
    content.VerifyStorage(check);
  } catch (const data::CookedFormatError& error) {
    return error.Defect();
  }
  return std::nullopt;
}

//! The rule `ReadCookedSummary` of `path` reports broken, if it fails.
auto SummaryDefect(const cooked::CookerStorageReader& storage,
  const fs::path& path) -> std::optional<data::CookedDefect>
{
  try {
    static_cast<void>(ReadCookedSummary(storage, path));
  } catch (const data::CookedFormatError& error) {
    return error.Defect();
  }
  return std::nullopt;
}

//! Flips one byte of the file `path` of `files`, keeping its size.
auto FlipByte(
  MemoryFileSystem& files, const fs::path& path, const uint64_t offset) -> void
{
  const auto current = files.Bytes(path);
  auto bytes = std::vector<std::byte>(current.begin(), current.end());
  bytes.at(static_cast<size_t>(offset)) ^= std::byte { 0xFFU };
  files.AddFile(path, bytes);
}

//! Appends one byte to the file `path` of `files`.
auto GrowFile(MemoryFileSystem& files, const fs::path& path) -> void
{
  const auto current = files.Bytes(path);
  auto bytes = std::vector<std::byte>(current.begin(), current.end());
  bytes.push_back(std::byte { 0U });
  files.AddFile(path, bytes);
}

//! Compares the fields of two texture rows that do not depend on where the
//! payload is stored.
auto ExpectSameTexture(const core::TextureResourceDesc& lhs,
  const core::TextureResourceDesc& rhs) -> void
{
  EXPECT_EQ(lhs.width, rhs.width);
  EXPECT_EQ(lhs.height, rhs.height);
  EXPECT_EQ(lhs.depth, rhs.depth);
  EXPECT_EQ(lhs.mip_levels, rhs.mip_levels);
  EXPECT_EQ(lhs.format, rhs.format);
  EXPECT_EQ(lhs.size_bytes, rhs.size_bytes);
}

//=== Fixtures ===------------------------------------------------------------//

//! The sample as a loose root and as a pak of it, in one in-memory workspace.
class CookedContentTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    sample_ = MakeSample();
    loose_ = workspace_.Add(sample_.library);
    const auto request = workspace_.FullRequest({ loose_ });
    static_cast<void>(workspace_.PackOrThrow(request));
    pak_ = PakWorkspace::PakSource(request.output_pak_path);
    pak_key_ = request.source_key;
  }

  [[nodiscard]] auto Storage() noexcept -> MemoryFileSystem&
  {
    return workspace_.Storage();
  }

  [[nodiscard]] auto SourceOf(const Flavor flavor) const
    -> const data::CookedSource&
  {
    return flavor == Flavor::kLoose ? loose_ : pak_;
  }

  [[nodiscard]] auto OpenAs(const Flavor flavor) -> CookedContent
  {
    return CookedContent::Open(Storage(), SourceOf(flavor));
  }

  PakWorkspace workspace_;
  Sample sample_;
  data::CookedSource loose_;
  data::CookedSource pak_;
  data::SourceKey pak_key_;
};

//! The same content, opened as each kind of source.
class CookedContentFlavorTest : public CookedContentTest,
                                public ::testing::WithParamInterface<Flavor> {
};

INSTANTIATE_TEST_SUITE_P(Sources, CookedContentFlavorTest,
  ::testing::Values(Flavor::kLoose, Flavor::kPak), FlavorName);

//=== What both kinds answer ===----------------------------------------------//

//! Whatever holds the bytes, the view states what kind of source it opened.
NOLINT_TEST_P(CookedContentFlavorTest, SourceStatesItsKindAndPath)
{
  // Arrange
  const auto flavor = GetParam();

  // Act
  const auto content = OpenAs(flavor);

  // Assert
  EXPECT_EQ(content.Source().kind,
    flavor == Flavor::kLoose ? data::CookedSourceKind::kLooseCooked
                             : data::CookedSourceKind::kPak);
  EXPECT_EQ(content.Source().path, SourceOf(flavor).path);
  EXPECT_EQ(content.Catalog() != nullptr, flavor == Flavor::kPak);
}

//! A folder opens as a loose root and a file as a pak, from the path alone.
NOLINT_TEST_P(CookedContentFlavorTest, OpeningAPathPicksTheKind)
{
  // Arrange
  const auto flavor = GetParam();

  // Act
  const auto content = CookedContent::Open(Storage(), SourceOf(flavor).path);

  // Assert
  EXPECT_EQ(content.Source().kind, SourceOf(flavor).kind);
  EXPECT_EQ(content.Assets().size(), 2U);
}

NOLINT_TEST_P(CookedContentFlavorTest, SummaryStatesWhatTheSourceProvides)
{
  // Arrange
  const auto flavor = GetParam();

  // Act
  const auto content = OpenAs(flavor);
  const auto& summary = content.Summary();

  // Assert
  EXPECT_EQ(summary.kind, SourceOf(flavor).kind);
  EXPECT_EQ(summary.source_key,
    flavor == Flavor::kLoose ? sample_.library.SourceKey() : pak_key_);
  EXPECT_THAT(summary.mounts, ::testing::ElementsAre("Game"));
  ASSERT_EQ(summary.assets.size(), 2U);
  EXPECT_EQ(summary.assets.at(sample_.material), data::AssetType::kMaterial);
  EXPECT_EQ(summary.assets.at(sample_.geometry), data::AssetType::kGeometry);
  EXPECT_THAT(summary.textures,
    ::testing::ElementsAre(data::TextureKeyFromVirtualPath(sample_.sky)));
  EXPECT_TRUE(summary.deleted.empty());
  EXPECT_TRUE(summary.requirements.empty());
  EXPECT_TRUE(summary.external_scripts.empty());
}

NOLINT_TEST_P(CookedContentFlavorTest, ReadCookedSummaryEqualsTheOpenedSummary)
{
  // Arrange
  const auto flavor = GetParam();
  const auto content = OpenAs(flavor);

  // Act
  const auto read = ReadCookedSummary(Storage(), SourceOf(flavor).path);

  // Assert
  const auto& opened = content.Summary();
  EXPECT_EQ(read.kind, opened.kind);
  EXPECT_EQ(read.source_key, opened.source_key);
  EXPECT_EQ(read.mounts, opened.mounts);
  EXPECT_EQ(read.assets, opened.assets);
  EXPECT_EQ(read.textures, opened.textures);
  EXPECT_EQ(read.deleted, opened.deleted);
  EXPECT_EQ(read.requirements, opened.requirements);
  EXPECT_EQ(read.external_scripts, opened.external_scripts);
}

NOLINT_TEST_P(CookedContentFlavorTest, AssetsAreSortedByKeyWithTheirFacts)
{
  // Arrange
  const auto content = OpenAs(GetParam());

  // Act
  const auto assets = content.Assets();

  // Assert
  ASSERT_EQ(assets.size(), 2U);
  EXPECT_LT(assets[0].key, assets[1].key);
  const auto* material = content.FindAsset(sample_.material);
  ASSERT_NE(material, nullptr);
  EXPECT_EQ(material->type, data::AssetType::kMaterial);
  EXPECT_EQ(material->virtual_path, kWoodPath);
  EXPECT_EQ(
    material->descriptor_size.get(), sample_.material_descriptor.size());
  EXPECT_EQ(material->descriptor_sha256,
    oxygen::base::ComputeSha256(sample_.material_descriptor));
  const auto* geometry = content.FindAsset(sample_.geometry);
  ASSERT_NE(geometry, nullptr);
  EXPECT_EQ(geometry->type, data::AssetType::kGeometry);
  EXPECT_EQ(geometry->virtual_path, kCratePath);
}

NOLINT_TEST_P(CookedContentFlavorTest, FindAssetByKeyAndByPathAgree)
{
  // Arrange
  const auto content = OpenAs(GetParam());

  // Act
  const auto* by_key = content.FindAsset(sample_.material);
  const auto* by_path = content.FindAsset(kWoodPath);

  // Assert
  ASSERT_NE(by_key, nullptr);
  EXPECT_EQ(by_key, by_path);
}

NOLINT_TEST_P(CookedContentFlavorTest, FindAssetOfAnUnknownKeyOrPathIsNull)
{
  // Arrange
  const auto content = OpenAs(GetParam());
  constexpr auto missing = std::string_view { "/Game/Missing.omat" };

  // Act
  const auto* by_key
    = content.FindAsset(data::AssetKey::FromVirtualPath(missing));
  const auto* by_path = content.FindAsset(missing);

  // Assert
  EXPECT_EQ(by_key, nullptr);
  EXPECT_EQ(by_path, nullptr);
}

NOLINT_TEST_P(CookedContentFlavorTest, AssetReferencesBindTheirResources)
{
  // Arrange
  const auto content = OpenAs(GetParam());
  auto wood = fixtures::MaterialBuilder { "Wood" };
  wood.Texture(&data::pak::render::MaterialAssetDesc::base_color_texture,
    sample_.texture_row);

  // Act
  const auto* material = content.FindAsset(sample_.material);
  const auto* geometry = content.FindAsset(sample_.geometry);

  // Assert
  ASSERT_NE(material, nullptr);
  ASSERT_NE(geometry, nullptr);
  EXPECT_EQ(material->references, wood.References());
  EXPECT_EQ(geometry->references,
    data::AssetReferences::Create(
      { { .kind = data::ResourceKind::kBuffer, .index = sample_.buffer_row } },
      {})
      .value());
}

NOLINT_TEST_P(CookedContentFlavorTest, DescriptorReturnsTheAuthoredBytes)
{
  // Arrange
  const auto content = OpenAs(GetParam());

  // Act
  const auto material = content.Descriptor(sample_.material);
  const auto geometry = content.Descriptor(sample_.geometry);

  // Assert
  EXPECT_EQ(material, sample_.material_descriptor);
  EXPECT_EQ(geometry, sample_.geometry_descriptor);
}

NOLINT_TEST_P(CookedContentFlavorTest, DescriptorOfAnUnknownKeyThrows)
{
  // Arrange
  const auto content = OpenAs(GetParam());
  const auto missing = data::AssetKey::FromVirtualPath("/Game/Missing.omat");

  // Act / Assert
  EXPECT_THROW(
    static_cast<void>(content.Descriptor(missing)), std::out_of_range);
  EXPECT_THROW(
    static_cast<void>(content.DescriptorStored(missing)), std::out_of_range);
}

NOLINT_TEST_P(
  CookedContentFlavorTest, DescriptorStoredLocatesTheDescriptorBytes)
{
  // Arrange
  const auto content = OpenAs(GetParam());

  // Act
  const auto stored = content.DescriptorStored(sample_.material);

  // Assert
  EXPECT_EQ(stored.range.count.get(), sample_.material_descriptor.size());
  const auto file = Storage().Bytes(stored.file);
  ASSERT_GE(file.size(), stored.range.offset.get() + stored.range.count.get());
  EXPECT_TRUE(std::equal(sample_.material_descriptor.begin(),
    sample_.material_descriptor.end(),
    file.begin() + static_cast<std::ptrdiff_t>(stored.range.offset.get())));
}

NOLINT_TEST_P(CookedContentFlavorTest, ResourceTablesHoldRowZeroAndTheirRows)
{
  // Arrange
  const auto content = OpenAs(GetParam());

  // Act
  const auto& textures = content.ResourceTable(data::ResourceKind::kTexture);
  const auto& buffers = content.ResourceTable(data::ResourceKind::kBuffer);

  // Assert
  EXPECT_EQ(textures.Kind(), cooked::TableKind::kTexture);
  EXPECT_EQ(textures.RowCount(), 2U);
  EXPECT_EQ(buffers.RowCount(), 2U);
  EXPECT_EQ(textures.RowBytes(sample_.texture_row).size(),
    cooked::CookedResourceTable::RowSize(cooked::TableKind::kTexture));
  auto expected = LooseCookedRoot::OnePixelTexture();
  expected.size_bytes = static_cast<uint32_t>(sample_.texture_payload.size());
  ExpectSameTexture(
    textures.Row<core::TextureResourceDesc>(sample_.texture_row), expected);
  EXPECT_EQ(textures.Payload(sample_.texture_row), sample_.texture_payload);
  EXPECT_EQ(buffers.Payload(sample_.buffer_row), sample_.buffer_payload);
}

NOLINT_TEST_P(CookedContentFlavorTest, SourceWithoutAScriptTableHasAnEmptyOne)
{
  // Arrange
  const auto content = OpenAs(GetParam());

  // Act
  const auto& scripts = content.ResourceTable(data::ResourceKind::kScript);

  // Assert
  EXPECT_EQ(scripts.RowCount(), 0U);
  EXPECT_THROW(static_cast<void>(scripts.RowBytes(ResourceIndexT { 0U })),
    std::out_of_range);
}

NOLINT_TEST_P(CookedContentFlavorTest, RowPastTheLastRowThrows)
{
  // Arrange
  const auto content = OpenAs(GetParam());
  const auto& textures = content.ResourceTable(data::ResourceKind::kTexture);

  // Act / Assert
  EXPECT_THROW(static_cast<void>(
                 textures.RowBytes(ResourceIndexT { textures.RowCount() })),
    std::out_of_range);
}

NOLINT_TEST_P(CookedContentFlavorTest, RowOfAnotherKindThrows)
{
  // Arrange
  const auto content = OpenAs(GetParam());
  const auto& textures = content.ResourceTable(data::ResourceKind::kTexture);

  // Act / Assert
  EXPECT_THROW(static_cast<void>(
                 textures.Row<core::BufferResourceDesc>(sample_.texture_row)),
    std::invalid_argument);
}

NOLINT_TEST_P(CookedContentFlavorTest, NamedTexturesBindTheirPathsToRows)
{
  // Arrange
  const auto content = OpenAs(GetParam());

  // Act
  const auto named = content.NamedTextures();
  const auto* found = content.FindNamedTexture(sample_.sky);

  // Assert
  ASSERT_EQ(named.size(), 1U);
  EXPECT_EQ(named.front().virtual_path, sample_.sky);
  EXPECT_EQ(named.front().row, sample_.texture_row);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->row, sample_.texture_row);
  EXPECT_EQ(content.FindNamedTexture("/Game/Textures/Missing.otex"), nullptr);
}

NOLINT_TEST_P(CookedContentFlavorTest, VerifyStoragePassesOnUntouchedBytes)
{
  // Arrange
  const auto content = OpenAs(GetParam());

  // Act / Assert
  EXPECT_NO_THROW(content.VerifyStorage(IntegrityCheck::kMetadata));
  EXPECT_NO_THROW(content.VerifyStorage(IntegrityCheck::kFull));
}

//=== One content, two sources ===--------------------------------------------//

//! The loose root and the pak of it answer every query the same way.
NOLINT_TEST_F(CookedContentTest, LooseRootAndItsPakAgree)
{
  // Arrange
  const auto loose = OpenAs(Flavor::kLoose);
  const auto packed = OpenAs(Flavor::kPak);

  // Act
  const auto loose_assets = loose.Assets();
  const auto packed_assets = packed.Assets();

  // Assert
  ASSERT_EQ(loose_assets.size(), packed_assets.size());
  for (size_t i = 0; i < loose_assets.size(); ++i) {
    EXPECT_EQ(loose_assets[i].key, packed_assets[i].key);
    EXPECT_EQ(loose_assets[i].type, packed_assets[i].type);
    EXPECT_EQ(loose_assets[i].virtual_path, packed_assets[i].virtual_path);
    EXPECT_EQ(loose_assets[i].references, packed_assets[i].references);
    EXPECT_EQ(loose_assets[i].descriptor_size.get(),
      packed_assets[i].descriptor_size.get());
    EXPECT_EQ(
      loose_assets[i].descriptor_sha256, packed_assets[i].descriptor_sha256);
    EXPECT_EQ(loose.Descriptor(loose_assets[i].key),
      packed.Descriptor(packed_assets[i].key));
  }
  EXPECT_EQ(loose.Summary().mounts, packed.Summary().mounts);
  EXPECT_EQ(loose.Summary().assets, packed.Summary().assets);
  EXPECT_EQ(loose.Summary().textures, packed.Summary().textures);
  for (const auto kind :
    { data::ResourceKind::kTexture, data::ResourceKind::kBuffer }) {
    EXPECT_EQ(loose.ResourceTable(kind).RowCount(),
      packed.ResourceTable(kind).RowCount());
    EXPECT_EQ(loose.ResourceTable(kind).Payload(ResourceIndexT { 1U }),
      packed.ResourceTable(kind).Payload(ResourceIndexT { 1U }));
  }
  ExpectSameTexture(loose.ResourceTable(data::ResourceKind::kTexture)
                      .Row<core::TextureResourceDesc>(sample_.texture_row),
    packed.ResourceTable(data::ResourceKind::kTexture)
      .Row<core::TextureResourceDesc>(sample_.texture_row));
  ASSERT_EQ(loose.NamedTextures().size(), packed.NamedTextures().size());
  EXPECT_EQ(loose.NamedTextures().front().virtual_path,
    packed.NamedTextures().front().virtual_path);
  EXPECT_EQ(
    loose.NamedTextures().front().row, packed.NamedTextures().front().row);
}

//! A pak states its catalog; a loose root has none.
NOLINT_TEST_F(CookedContentTest, OnlyAPakHoldsACatalog)
{
  // Arrange / Act
  const auto loose = OpenAs(Flavor::kLoose);
  const auto packed = OpenAs(Flavor::kPak);

  // Assert
  EXPECT_EQ(loose.Catalog(), nullptr);
  ASSERT_NE(packed.Catalog(), nullptr);
  EXPECT_EQ(packed.Catalog()->mount, "Game");
  EXPECT_EQ(packed.Catalog()->source_key, pak_key_);
}

//! Only a loose root holds sidecars: the records a pak build reads.
NOLINT_TEST_F(CookedContentTest, SidecarsBelongToLooseRootsOnly)
{
  // Arrange
  auto library = LooseCookedRoot { "Game" };
  library.AddFile(lc::FileKind::kAuxiliary, "Buffers/Mesh.obuf", SeedBytes(7U));
  const auto source = workspace_.Add(library);

  // Act
  const auto content = CookedContent::Open(Storage(), source);

  // Assert
  ASSERT_EQ(content.Sidecars().size(), 1U);
  const auto& sidecar = content.Sidecars().front();
  EXPECT_EQ(sidecar.name, "Buffers/Mesh.obuf");
  EXPECT_EQ(sidecar.stored.range.count.get(), SeedBytes(7U).size());
  EXPECT_TRUE(OpenAs(Flavor::kPak).Sidecars().empty());
}

//! A moved content keeps reading the source it opened.
NOLINT_TEST_F(CookedContentTest, MovedContentKeepsItsSource)
{
  // Arrange
  auto original = OpenAs(Flavor::kLoose);

  // Act
  auto moved = std::move(original);
  auto assigned = OpenAs(Flavor::kPak);
  assigned = std::move(moved);

  // Assert
  EXPECT_EQ(assigned.Source().kind, data::CookedSourceKind::kLooseCooked);
  EXPECT_EQ(assigned.Descriptor(sample_.material), sample_.material_descriptor);
}

//=== Failures to open ===----------------------------------------------------//

NOLINT_TEST_F(CookedContentTest, FolderWithoutAnIndexIsWrongFormat)
{
  // Arrange
  const auto folder = fs::path("/cooked/Empty");
  Storage().AddFolder(folder);

  // Act / Assert
  EXPECT_EQ(OpenDefect(Storage(), folder), data::CookedDefect::kAbsent);
  EXPECT_EQ(SummaryDefect(Storage(), folder), data::CookedDefect::kAbsent);
}

NOLINT_TEST_F(CookedContentTest, FileThatIsNotAPakIsWrongFormat)
{
  // Arrange
  const auto file = fs::path("/paks/notapak.bin");
  Storage().AddFile(file, std::vector<std::byte>(16));

  // Act / Assert
  EXPECT_EQ(OpenDefect(Storage(), file), data::CookedDefect::kTooSmall);
}

NOLINT_TEST_F(CookedContentTest, MissingPathIsAbsent)
{
  // Arrange
  const auto missing = fs::path("/paks/missing.pak");

  // Act / Assert
  EXPECT_EQ(OpenDefect(Storage(), missing), data::CookedDefect::kAbsent);
}

//! A named texture whose descriptor disagrees with its table row is corrupt.
NOLINT_TEST_F(
  CookedContentTest, TextureDescriptorDisagreeingWithItsRowIsCorrupt)
{
  // Arrange
  auto library = LooseCookedRoot { "Game" };
  const auto row = library.AddTexture(SeedBytes(1U));
  auto forged = LooseCookedRoot::OnePixelTexture();
  forged.width = 2U;
  library.AddFile(lc::FileKind::kTextureDescriptor, "Textures/Forged.otex",
    data::EncodeTextureResourceDescriptor(
      { .index = row, .descriptor = forged }),
    library.VirtualPath("Textures/Forged.otex"));
  const auto source = workspace_.Add(library);

  // Act / Assert
  EXPECT_EQ(
    OpenDefect(Storage(), source.path), data::CookedDefect::kInconsistent);
}

NOLINT_TEST_F(CookedContentTest, TextureDescriptorNamingAMissingRowIsCorrupt)
{
  // Arrange
  auto library = LooseCookedRoot { "Game" };
  static_cast<void>(library.AddTexture(SeedBytes(1U)));
  library.AddFile(lc::FileKind::kTextureDescriptor, "Textures/Lost.otex",
    data::EncodeTextureResourceDescriptor({
      .index = ResourceIndexT { 9U },
      .descriptor = LooseCookedRoot::OnePixelTexture(),
    }),
    library.VirtualPath("Textures/Lost.otex"));
  const auto source = workspace_.Add(library);

  // Act / Assert
  EXPECT_EQ(
    OpenDefect(Storage(), source.path), data::CookedDefect::kOutOfBounds);
}

NOLINT_TEST_F(CookedContentTest, TableHoldingAPartialRowIsCorrupt)
{
  // Arrange
  // The encoder refuses such a table, so the index is damaged after it.
  auto library = LooseCookedRoot { "Game" };
  static_cast<void>(library.AddTexture(SeedBytes(1U)));
  auto files = library.Files();
  CookedBytes::GivePartialRow(files, lc::FileKind::kTexturesTable);
  Storage().AddFiles("/cooked/Partial", files);

  // Act / Assert
  EXPECT_EQ(OpenDefect(Storage(), "/cooked/Partial"),
    data::CookedDefect::kWrongRowSize);
}

//=== Summaries without loading ===-------------------------------------------//

//! A script is external when its descriptor names a source outside the root.
NOLINT_TEST_F(CookedContentTest, LooseSummaryListsExternalScripts)
{
  // Arrange
  const auto add_script = [](LooseCookedRoot& library, const std::string& leaf,
                            const bool external) -> void {
    auto script = ScriptBuilder { "Script" };
    if (external) {
      script.Flags(data::pak::scripting::ScriptAssetFlags::kAllowExternalSource)
        .ExternalSource(library.VirtualPath("Script.lua"));
    }
    static_cast<void>(library.Script(leaf, script));
  };
  auto library = LooseCookedRoot { "Props" };
  add_script(library, "Scripts/Spin.oscript", true);
  add_script(library, "Scripts/Embedded.oscript", false);
  const auto source = workspace_.Add(library);

  // Act
  const auto read = ReadCookedSummary(Storage(), source.path);
  const auto opened = CookedContent::Open(Storage(), source);

  // Assert
  const auto expected
    = std::vector<std::string> { "/Props/Scripts/Spin.oscript" };
  EXPECT_EQ(read.external_scripts, expected);
  EXPECT_EQ(opened.Summary().external_scripts, expected);
}

//! A loose root states its mounts from the virtual paths of its assets and
//! textures.
NOLINT_TEST_F(CookedContentTest, LooseSummaryStatesEveryMount)
{
  // Arrange
  Storage().AddFiles("/cooked/Mixed", MixedMountFiles());

  // Act
  const auto summary = ReadCookedSummary(Storage(), "/cooked/Mixed");

  // Assert
  EXPECT_EQ(summary.mounts, (std::vector<std::string> { "Props", "Vehicles" }));
  EXPECT_EQ(summary.assets.size(), 3U);
  EXPECT_THAT(summary.textures,
    ::testing::ElementsAre(
      data::TextureKeyFromVirtualPath("/Props/Wood.otex")));
  EXPECT_TRUE(summary.requirements.empty());
}

NOLINT_TEST_F(CookedContentTest, IndexOfAnotherVersionIsUnsupported)
{
  // Arrange
  auto files = LooseCookedRoot { "Game" }.Files();
  CookedBytes::SetIndexVersion(
    files, static_cast<uint16_t>(lc::kIndexVersion + 1U));
  Storage().AddFiles("/cooked/Future", files);

  // Act / Assert
  EXPECT_EQ(SummaryDefect(Storage(), "/cooked/Future"),
    data::CookedDefect::kUnsupportedVersion);
  EXPECT_EQ(OpenDefect(Storage(), "/cooked/Future"),
    data::CookedDefect::kUnsupportedVersion);
}

NOLINT_TEST_F(CookedContentTest, TruncatedIndexIsCorrupt)
{
  // Arrange
  auto files = sample_.library.Files();
  files.at("container.index.bin").pop_back();
  Storage().AddFiles("/cooked/Truncated", files);

  // Act / Assert
  // The index's last section now ends past the file.
  EXPECT_EQ(SummaryDefect(Storage(), "/cooked/Truncated"),
    data::CookedDefect::kOutOfBounds);
}

//! A pak states its library as its one mount, the assets its catalog lists,
//! what it deletes and what it requires of other mounts.
NOLINT_TEST_F(CookedContentTest, PakSummaryStatesItsCatalog)
{
  // Arrange
  auto engine = LooseCookedRoot { "Engine" };
  const auto default_material = engine.Material("Default.omat");
  auto base = LooseCookedRoot { "Props" };
  const auto removed = base.Geometry("Old.ogeo");
  auto props = LooseCookedRoot { "Props" };
  auto chair_geometry = fixtures::GeometryBuilder { "Chair" };
  chair_geometry.Submesh(chair_geometry.ProceduralLod(),
    data::SurfaceId::FromStableIdentity("chair"), default_material);
  const auto chair = props.Geometry("Chair.ogeo", chair_geometry);
  const auto requirement = data::PakRequirement {
    .kind = data::KeyReferenceKind::kAsset,
    .key = default_material,
    .type = data::AssetType::kMaterial,
  };
  Storage().AddFile("/paks/Props.pak",
    CookedPak { std::move(props) }
      .PatchOf(CookedPak { std::move(base) })
      .DependsOn(CookedPak { std::move(engine) })
      .Encode());

  // Act
  const auto summary = ReadCookedSummary(Storage(), "/paks/Props.pak");

  // Assert
  EXPECT_EQ(summary.kind, data::CookedSourceKind::kPak);
  EXPECT_THAT(summary.mounts, ::testing::ElementsAre("Props"));
  EXPECT_THAT(summary.assets,
    ::testing::ElementsAre(::testing::Pair(chair, data::AssetType::kGeometry)));
  EXPECT_THAT(summary.deleted, ::testing::ElementsAre(removed));
  ASSERT_EQ(summary.requirements.size(), 1U);
  EXPECT_EQ(summary.requirements.front().mount, "Engine");
  EXPECT_THAT(summary.requirements.front().requirements,
    ::testing::ElementsAre(requirement));
}

//=== Damaged paks ===--------------------------------------------------------//

//! How a damaged pak is built, and how it is refused.
struct PakDefect final {
  const char* name;
  std::function<std::vector<std::byte>()> make;
  data::CookedDefect defect;
};

auto DefectName(const ::testing::TestParamInfo<PakDefect>& info) -> std::string
{
  return info.param.name;
}

auto PakWithoutBrowseIndex() -> std::vector<std::byte>
{
  auto bytes = ValidPakBytes();
  CookedBytes::DropBrowseIndex(bytes);
  return bytes;
}

auto PakOfAnotherVersion() -> std::vector<std::byte>
{
  auto bytes = ValidPakBytes();
  CookedBytes::SetPakVersion(
    bytes, static_cast<uint16_t>(core::kCurrentPakFormatVersion + 1U));
  return bytes;
}

auto PakWithOtherMagic() -> std::vector<std::byte>
{
  auto bytes = ValidPakBytes();
  CookedBytes::BreakPakMagic(bytes);
  return bytes;
}

auto PakTooSmall() -> std::vector<std::byte>
{
  return std::vector<std::byte>(16);
}

auto PakWithDamagedFooter() -> std::vector<std::byte>
{
  auto bytes = ValidPakBytes();
  CookedBytes::BreakFooterMagic(bytes);
  return bytes;
}

class CookedContentPakDefectTest
  : public CookedContentTest,
    public ::testing::WithParamInterface<PakDefect> { };

INSTANTIATE_TEST_SUITE_P(Defects, CookedContentPakDefectTest,
  ::testing::Values(PakDefect { "WithoutBrowseIndex", PakWithoutBrowseIndex,
                      data::CookedDefect::kMissingPart },
    PakDefect { "OfAnotherVersion", PakOfAnotherVersion,
      data::CookedDefect::kUnsupportedVersion },
    PakDefect {
      "WithOtherMagic", PakWithOtherMagic, data::CookedDefect::kWrongMagic },
    PakDefect { "TooSmall", PakTooSmall, data::CookedDefect::kTooSmall },
    PakDefect { "WithDamagedFooter", PakWithDamagedFooter,
      data::CookedDefect::kTruncated }),
  DefectName);

NOLINT_TEST_P(CookedContentPakDefectTest, IsRefusedWithItsFailure)
{
  // Arrange
  const auto& defect = GetParam();
  Storage().AddFile("/paks/defect.pak", defect.make());

  // Act / Assert
  EXPECT_EQ(SummaryDefect(Storage(), "/paks/defect.pak"), defect.defect);
  EXPECT_EQ(OpenDefect(Storage(), "/paks/defect.pak"), defect.defect);
}

//=== Verifying storage ===---------------------------------------------------//

//! A loose descriptor that changed keeping its size fails the full check only.
NOLINT_TEST_F(CookedContentTest, VerifyStorageCatchesADescriptorOfAnotherDigest)
{
  // Arrange
  const auto stored = OpenAs(Flavor::kLoose).DescriptorStored(sample_.material);
  FlipByte(Storage(), stored.file, 0U);
  const auto content = OpenAs(Flavor::kLoose);

  // Act / Assert
  EXPECT_EQ(VerifyDefect(content, IntegrityCheck::kMetadata), std::nullopt);
  EXPECT_EQ(VerifyDefect(content, IntegrityCheck::kFull),
    data::CookedDefect::kDigestMismatch);
}

//! A loose descriptor that changed its size fails even the cheap check.
NOLINT_TEST_F(CookedContentTest, VerifyStorageCatchesADescriptorOfAnotherSize)
{
  // Arrange
  const auto stored = OpenAs(Flavor::kLoose).DescriptorStored(sample_.material);
  GrowFile(Storage(), stored.file);
  const auto content = OpenAs(Flavor::kLoose);

  // Act / Assert
  EXPECT_EQ(VerifyDefect(content, IntegrityCheck::kMetadata),
    data::CookedDefect::kSizeMismatch);
}

NOLINT_TEST_F(
  CookedContentTest, VerifyStorageCatchesAResourceFileOfAnotherDigest)
{
  // Arrange
  const auto data_file = OpenAs(Flavor::kLoose)
                           .ResourceTable(data::ResourceKind::kTexture)
                           .DataStored()
                           .file;
  FlipByte(Storage(), data_file, 0U);
  const auto content = OpenAs(Flavor::kLoose);

  // Act / Assert
  EXPECT_EQ(VerifyDefect(content, IntegrityCheck::kMetadata), std::nullopt);
  EXPECT_EQ(VerifyDefect(content, IntegrityCheck::kFull),
    data::CookedDefect::kDigestMismatch);
}

//! A changed resource payload fails the full check, which names the row whose
//! SHA-256 the bytes no longer have.
NOLINT_TEST_F(CookedContentTest, VerifyStorageNamesTheResourceRowThatChanged)
{
  // Arrange
  const auto stored = OpenAs(Flavor::kLoose)
                        .ResourceTable(data::ResourceKind::kBuffer)
                        .PayloadStored(sample_.buffer_row);
  FlipByte(Storage(), stored.file, stored.range.offset.get());
  const auto content = OpenAs(Flavor::kLoose);

  // Act
  auto message = std::string {};
  try {
    content.VerifyStorage(IntegrityCheck::kFull);
  } catch (const std::runtime_error& error) {
    message = error.what();
  }

  // Assert
  EXPECT_THAT(message,
    ::testing::HasSubstr("row " + std::to_string(sample_.buffer_row.get())));
  EXPECT_NO_THROW(content.VerifyStorage(IntegrityCheck::kMetadata));
}

//! A member file removed after the root was opened fails even the cheap
//! check.
NOLINT_TEST_F(CookedContentTest, VerifyStorageCatchesAMissingMemberFile)
{
  // Arrange
  const auto content = OpenAs(Flavor::kLoose);
  ASSERT_TRUE(Storage().Remove(
    content.ResourceTable(data::ResourceKind::kBuffer).DataStored().file));

  // Act / Assert
  EXPECT_EQ(VerifyDefect(content, IntegrityCheck::kMetadata),
    data::CookedDefect::kMissingPart);
}

//! A file the index does not list fails even the cheap check, as it fails the
//! engine's check before a mount: the root holds what its index does not
//! describe.
NOLINT_TEST_F(CookedContentTest, VerifyStorageCatchesAnUnindexedFile)
{
  // Arrange
  const auto content = OpenAs(Flavor::kLoose);
  const auto stray = content.Source().path / "Materials/Stray.omat";
  Storage().AddFile(stray, std::vector { std::byte { 0x5A } });

  // Act / Assert
  try {
    content.VerifyStorage(IntegrityCheck::kMetadata);
    ADD_FAILURE() << "the unindexed file verified";
  } catch (const data::CookedFormatError& error) {
    EXPECT_EQ(error.Defect(), data::CookedDefect::kInconsistent);
    EXPECT_THAT(error.what(), ::testing::HasSubstr(stray.generic_string()));
  }
}

//! A resource row placing its payload past its data file fails even the cheap
//! check, which reads the rows without reading the payloads.
NOLINT_TEST_F(CookedContentTest, VerifyStorageCatchesARowOutsideItsDataFile)
{
  // Arrange
  const auto content = OpenAs(Flavor::kLoose);
  const auto& table = content.ResourceTable(data::ResourceKind::kBuffer);
  const auto offset_high_byte = table.RowsStored().range.offset.get()
    + (static_cast<uint64_t>(sample_.buffer_row.get())
      * cooked::CookedResourceTable::RowSize(cooked::TableKind::kBuffer))
    + offsetof(core::BufferResourceDesc, data_offset)
    + sizeof(core::BufferResourceDesc::data_offset) - 1U;
  FlipByte(Storage(), table.RowsStored().file, offset_high_byte);

  // Act / Assert
  EXPECT_EQ(VerifyDefect(content, IntegrityCheck::kMetadata),
    data::CookedDefect::kOutOfBounds);
}

//! The indexed size is checked before a changed descriptor's digest.
NOLINT_TEST_F(CookedContentTest, VerifyStorageReportsSizeBeforeDigest)
{
  // Arrange
  const auto content = OpenAs(Flavor::kLoose);
  const auto stored = content.DescriptorStored(sample_.material);
  FlipByte(Storage(), stored.file, 0U);
  GrowFile(Storage(), stored.file);

  // Act / Assert
  try {
    content.VerifyStorage(IntegrityCheck::kFull);
    ADD_FAILURE() << "the changed descriptor verified";
  } catch (const data::CookedFormatError& error) {
    EXPECT_EQ(error.Defect(), data::CookedDefect::kSizeMismatch);
    EXPECT_THAT(
      error.what(), ::testing::HasSubstr(stored.file.generic_string()));
  }
}

//! The first missing indexed file is reported in inventory path order.
NOLINT_TEST_F(CookedContentTest, VerifyStorageReportsFirstMissingFileByPath)
{
  // Arrange
  const auto content = OpenAs(Flavor::kLoose);
  const auto material = content.DescriptorStored(sample_.material).file;
  const auto geometry = content.DescriptorStored(sample_.geometry).file;
  ASSERT_TRUE(Storage().Remove(material));
  ASSERT_TRUE(Storage().Remove(geometry));
  const auto first
    = std::min(material.generic_string(), geometry.generic_string());

  // Act / Assert
  try {
    content.VerifyStorage(IntegrityCheck::kMetadata);
    ADD_FAILURE() << "the missing files verified";
  } catch (const data::CookedFormatError& error) {
    EXPECT_EQ(error.Defect(), data::CookedDefect::kMissingPart);
    EXPECT_THAT(error.what(), ::testing::HasSubstr(first));
  }
}

//! The metadata check compares no digest, a row's included; the full check
//! hashes the table that holds it.
NOLINT_TEST_F(CookedContentTest, VerifyStorageMetadataComparesNoRowDigest)
{
  // Arrange
  const auto content = OpenAs(Flavor::kLoose);
  const auto& table = content.ResourceTable(data::ResourceKind::kBuffer);
  const auto& rows = table.RowsStored();
  const auto offset = rows.range.offset.get()
    + (static_cast<uint64_t>(sample_.buffer_row.get())
      * cooked::CookedResourceTable::RowSize(cooked::TableKind::kBuffer))
    + offsetof(core::BufferResourceDesc, content_hash);
  FlipByte(Storage(), rows.file, offset);

  // Act / Assert
  EXPECT_NO_THROW(content.VerifyStorage(IntegrityCheck::kMetadata));
  try {
    content.VerifyStorage(IntegrityCheck::kFull);
    ADD_FAILURE() << "the changed resource table verified";
  } catch (const data::CookedFormatError& error) {
    EXPECT_EQ(error.Defect(), data::CookedDefect::kDigestMismatch);
    EXPECT_THAT(error.what(), ::testing::HasSubstr(rows.file.generic_string()));
  }
}

//! A pak's check is its CRC, which only the full check reads.
NOLINT_TEST_F(CookedContentTest, VerifyStorageCatchesAPakOfAnotherCrc)
{
  // Arrange
  const auto stored = OpenAs(Flavor::kPak).DescriptorStored(sample_.material);
  FlipByte(Storage(), stored.file, stored.range.offset.get());
  const auto content = OpenAs(Flavor::kPak);

  // Act / Assert
  EXPECT_NO_THROW(content.VerifyStorage(IntegrityCheck::kMetadata));
  EXPECT_THROW(content.VerifyStorage(IntegrityCheck::kFull), std::exception);
}

} // namespace
