//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Jobs/MaterialDescriptorImportJob.cpp

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/ImportHarness.h>
#include <Oxygen/Cooker/test/Support/TextureLibraryFixture.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  using data::pak::core::kNoResourceIndex;
  using data::pak::core::ResourceIndexT;
  using data::pak::core::TextureResourceDesc;
  using oxygen::cooker::test::ImportServiceTest;
  using oxygen::cooker::test::ReadBytes;
  using oxygen::cooker::test::WriteBytes;

#pragma pack(push, 1)
  struct TextureSidecarFile final {
    // NOLINTNEXTLINE(*-avoid-c-arrays) - fixed on-disk layout
    char magic[4] = { 'O', 'T', 'E', 'X' };
    uint16_t version = 1;
    uint16_t reserved = 0;
    ResourceIndexT resource_index = kNoResourceIndex;
    TextureResourceDesc descriptor {};
  };
#pragma pack(pop)

  static_assert(std::is_trivially_copyable_v<TextureSidecarFile>);

  auto WriteTextureSidecar(const std::filesystem::path& path,
    const ResourceIndexT resource_index) -> void
  {
    auto file = TextureSidecarFile {};
    file.resource_index = resource_index;
    WriteBytes(
      path, std::as_bytes(std::span<const TextureSidecarFile, 1>(&file, 1)));
  }

  auto ReadMaterialDesc(const std::vector<std::byte>& bytes)
    -> data::pak::render::MaterialAssetDesc
  {
    auto desc = data::pak::render::MaterialAssetDesc {};
    if (bytes.size() < sizeof(desc)) {
      return desc;
    }
    std::memcpy(&desc, bytes.data(), sizeof(desc));
    return desc;
  }

  using oxygen::cooker::test::HasDiagnosticCode;

  class MaterialDescriptorImportJobTest : public ImportServiceTest {
  protected:
    auto MakeRequest(const std::filesystem::path& cooked_root,
      std::string descriptor_json,
      std::string_view asset_virtual_path
      = "/.cooked/Materials/woodfloor007.omat",
      std::string_view job_name = "woodfloor007") -> ImportRequest
    {
      auto request = ImportRequest {};
      request.source_path = "inline://woodfloor007.omat.json";
      request.job_name = std::string(job_name);
      request.asset_virtual_path = std::string(asset_virtual_path);
      request.cooked_root = cooked_root;
      request.loose_cooked_layout.virtual_mount_root = "/.cooked";
      request.material_descriptor = ImportRequest::MaterialDescriptorPayload {
        .normalized_descriptor_json = std::move(descriptor_json),
      };
      return request;
    }
  };

  //! A material job whose request names no identity cannot place its
  //! descriptor and is refused.
  NOLINT_TEST_F(
    MaterialDescriptorImportJobTest, RequestWithoutAssetVirtualPathIsRefused)
  {
    const auto cooked_root = TempDir() / "missing_asset_virtual_path";
    std::filesystem::create_directories(cooked_root);
    auto request = MakeRequest(cooked_root, R"({ "name": "woodfloor007" })");
    request.asset_virtual_path.clear();

    const auto report = Import(std::move(request));

    ASSERT_HAS_VALUE(report);
    EXPECT_FALSE(report->success);
    EXPECT_EQ(report->materials_written, 0U);
    EXPECT_TRUE(HasDiagnosticCode(
      report->diagnostics, "import.asset_virtual_path_missing"));
  }

  //! A source cooking its material again replaces it; another source cooking
  //! the same identity is refused, and the index keeps the first one's.
  NOLINT_TEST_F(
    MaterialDescriptorImportJobTest, AnotherSourceOfTheIdentityIsRefused)
  {
    const auto cooked_root = TempDir() / "identity_collision";
    std::filesystem::create_directories(cooked_root);
    const auto from = [&](std::string_view file) -> ImportRequest {
      auto request = MakeRequest(cooked_root, R"({ "name": "woodfloor007" })");
      request.source_path = TempDir() / file;
      return request;
    };

    const auto first = Import(from("woodfloor007.omat.json"));
    const auto again = Import(from("woodfloor007.omat.json"));
    const auto other = Import(from("Woodfloor007.omat.json"));

    ASSERT_HAS_VALUE(first);
    ASSERT_HAS_VALUE(again);
    ASSERT_HAS_VALUE(other);
    EXPECT_TRUE(first->success);
    EXPECT_TRUE(again->success) << "a source re-cooking its asset replaces it";
    EXPECT_FALSE(other->success);
    EXPECT_TRUE(
      HasDiagnosticCode(other->diagnostics, "import.identity_collision"));
    const auto index = lc::LooseCookedIndex::LoadFromRoot(cooked_root);
    const auto key
      = index.FindAssetKeyByVirtualPath("/.cooked/Materials/woodfloor007.omat");
    ASSERT_HAS_VALUE(key);
    EXPECT_EQ(index.FindSourcePath(*key),
      std::optional<std::string_view>(
        "/.cooked/Materials/woodfloor007.omat.json"));
  }

  NOLINT_TEST_F(MaterialDescriptorImportJobTest,
    CompilesEmissionColorAndIntensityWithoutBinary16Rounding)
  {
    const auto cooked_root = TempDir() / "float32_emission";
    std::filesystem::create_directories(cooked_root);
    const auto report = Import(MakeRequest(cooked_root, R"({
      "name": "HdrEmission",
      "parameters": {
        "emissive_color": [1.0, 0.25, 0.0],
        "emissive_intensity": 9.7
      }
    })",
      "/.cooked/Materials/HdrEmission.omat", "HdrEmission"));
    ASSERT_HAS_VALUE(report);
    ASSERT_TRUE(report->success);
    const auto bytes
      = ReadBytes(cooked_root / "Materials" / "HdrEmission.omat");
    ASSERT_GE(bytes.size(), sizeof(data::pak::render::MaterialAssetDesc));
    const auto desc = ReadMaterialDesc(bytes);
    EXPECT_EQ(desc.header.version, data::pak::render::kMaterialAssetVersion);
    EXPECT_EQ(desc.emissive_factor[0], 9.7F);
    EXPECT_EQ(desc.emissive_factor[1], 9.7F * 0.25F);
    EXPECT_EQ(desc.emissive_factor[2], 0.0F);
  }

  //! A texture reference names the descriptor at the texture's identity.
  NOLINT_TEST_F(MaterialDescriptorImportJobTest,
    ResolvesTextureDescriptorAtItsIdentityAndEmitsMaterial)
  {
    const auto cooked_root = TempDir() / "resolve_texture_virtual_path";
    std::filesystem::create_directories(cooked_root);
    const auto sidecar_path
      = cooked_root / "Resources" / "Textures" / "woodfloor007_color.otex";
    constexpr auto kTextureIndex = ResourceIndexT { 7U };
    WriteTextureSidecar(sidecar_path, kTextureIndex);

    const auto report = Import(MakeRequest(cooked_root, R"({
      "name": "woodfloor007",
      "domain": "opaque",
      "textures": {
        "base_color": {
          "virtual_path": "/.cooked/Resources/Textures/woodfloor007_color.otex"
        }
      }
    })"));

    ASSERT_HAS_VALUE(report);
    EXPECT_TRUE(report->success);
    EXPECT_EQ(report->materials_written, 1U);
    EXPECT_FALSE(HasDiagnosticCode(
      report->diagnostics, "material.descriptor.texture_descriptor_missing"));

    const auto material_path = cooked_root / "Materials" / "woodfloor007.omat";
    ASSERT_TRUE(std::filesystem::exists(material_path));
    const auto bytes = ReadBytes(material_path);
    ASSERT_GE(bytes.size(), sizeof(data::pak::render::MaterialAssetDesc));
    const auto desc = ReadMaterialDesc(bytes);
    EXPECT_EQ(desc.base_color_texture.get(), 0U);
    const auto index = lc::LooseCookedIndex::LoadFromRoot(cooked_root);
    const auto key
      = index.FindAssetKeyByVirtualPath("/.cooked/Materials/woodfloor007.omat");
    ASSERT_HAS_VALUE(key);
    // NOLINTBEGIN(bugprone-unchecked-optional-access) - asserted above
    const auto references = index.FindAssetReferences(key.value());
    ASSERT_HAS_VALUE(references);
    EXPECT_THAT(references.value().Resources(),
      ::testing::ElementsAre(data::ResourceBinding {
        .kind = data::ResourceKind::kTexture,
        .index = kTextureIndex,
      }));
    // NOLINTEND(bugprone-unchecked-optional-access)
  }

  //! A descriptor under another name, such as a hashed one, is not the
  //! texture: the reference is missing.
  NOLINT_TEST_F(
    MaterialDescriptorImportJobTest, IgnoresDescriptorsUnderAnotherName)
  {
    const auto cooked_root = TempDir() / "ignore_hashed_texture_name";
    std::filesystem::create_directories(cooked_root);
    WriteTextureSidecar(cooked_root / "Resources" / "Textures"
        / "woodfloor007_color_1234567890abcdef.otex",
      ResourceIndexT { 7U });

    const auto report = Import(MakeRequest(cooked_root, R"({
      "name": "woodfloor007",
      "domain": "opaque",
      "textures": {
        "base_color": {
          "virtual_path": "/.cooked/Resources/Textures/woodfloor007_color.otex"
        }
      }
    })"));

    ASSERT_HAS_VALUE(report);
    EXPECT_TRUE(HasDiagnosticCode(
      report->diagnostics, "material.descriptor.texture_descriptor_missing"));
  }

  //! A deleted texture leaves its slot unbound with a warning, so the material
  //! and everything that depends on it still cook.
  NOLINT_TEST_F(MaterialDescriptorImportJobTest,
    MissingTextureDescriptorCooksMaterialWithoutTextureAndWarns)
  {
    const auto cooked_root = TempDir() / "missing_texture_descriptor";
    std::filesystem::create_directories(cooked_root);
    const auto report = Import(MakeRequest(cooked_root, R"({
      "name": "woodfloor007",
      "domain": "opaque",
      "textures": {
        "base_color": {
          "virtual_path": "/.cooked/Resources/Textures/woodfloor007_color.otex"
        }
      }
    })"));

    ASSERT_HAS_VALUE(report);
    EXPECT_TRUE(report->success);
    EXPECT_EQ(report->materials_written, 1U);
    const auto missing = std::ranges::find_if(
      report->diagnostics, [](const ImportDiagnostic& diagnostic) -> bool {
        return diagnostic.code
          == "material.descriptor.texture_descriptor_missing";
      });
    ASSERT_NE(missing, report->diagnostics.end());
    EXPECT_EQ(missing->severity, ImportSeverity::kWarning);

    const auto bytes
      = ReadBytes(cooked_root / "Materials" / "woodfloor007.omat");
    ASSERT_GE(bytes.size(), sizeof(data::pak::render::MaterialAssetDesc));
    EXPECT_EQ(
      ReadMaterialDesc(bytes).base_color_texture, data::kNoResourceReference);
  }

  //! A texture a cooked context root holds, loose or packed, binds by key: the
  //! material references it without the texture being copied into its root.
  NOLINT_TEST_F(
    MaterialDescriptorImportJobTest, ContextRootTextureBindsByKeyWithoutCopying)
  {
    // Arrange
    constexpr auto kGrid = std::string_view { "/Library/Textures/Grid.otex" };
    const auto library = TempDir() / "library";
    oxygen::cooker::test::WriteTextureLibrary(library, kGrid,
      { .size_bytes = 4U,
        .width = 1U,
        .height = 1U,
        .depth = 1U,
        .array_layers = 1U,
        .mip_levels = 1U });
    const auto pak
      = oxygen::cooker::test::PackLibrary(library, TempDir() / "library.pak");
    constexpr auto kDescriptor = R"({
      "name": "Tiles",
      "domain": "opaque",
      "textures": {
        "base_color": { "virtual_path": "/Library/Textures/Grid.otex" },
        "normal": { "virtual_path": "/Library/Textures/Grid.otex" }
      }
    })";

    for (const auto& context : { library, pak }) {
      const auto cooked_root
        = TempDir() / ("tiles_" + context.filename().string());
      std::filesystem::create_directories(cooked_root);
      auto request = MakeRequest(
        cooked_root, kDescriptor, "/.cooked/Materials/Tiles.omat", "Tiles");
      request.cooked_context_roots = { context };

      // Act
      const auto report = Import(std::move(request));

      // Assert
      ASSERT_HAS_VALUE(report);
      EXPECT_TRUE(report->success) << context;
      const auto desc
        = ReadMaterialDesc(ReadBytes(cooked_root / "Materials" / "Tiles.omat"));
      EXPECT_EQ(desc.base_color_texture.get(), 0U);
      EXPECT_EQ(desc.normal_texture.get(), 0U);
      const auto index = lc::LooseCookedIndex::LoadFromRoot(cooked_root);
      const auto key
        = index.FindAssetKeyByVirtualPath("/.cooked/Materials/Tiles.omat");
      ASSERT_HAS_VALUE(key);
      // NOLINTBEGIN(bugprone-unchecked-optional-access) - asserted above
      const auto references = index.FindAssetReferences(key.value());
      ASSERT_HAS_VALUE(references);
      const auto target
        = references.value().ResolveTexture(desc.base_color_texture);
      ASSERT_TRUE(target.has_value());
      EXPECT_EQ(std::get<data::AssetKey>(*target),
        data::TextureKeyFromVirtualPath(kGrid));
      // NOLINTEND(bugprone-unchecked-optional-access)
    }
  }

} // namespace

} // namespace oxygen::content::import::test
