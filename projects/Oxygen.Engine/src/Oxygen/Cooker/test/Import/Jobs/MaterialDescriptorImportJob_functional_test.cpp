//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Jobs/MaterialDescriptorImportJob.cpp

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/test/Support/CookedFiles.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/ImportHarness.h>
#include <Oxygen/Cooker/testing/CookedPak.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  using oxygen::cooker::test::CookedFile;
  using oxygen::cooker::test::HasCookedFile;
  using oxygen::cooker::test::ImportServiceTest;
  using oxygen::cooker::test::ReadBytes;
  using oxygen::cooker::testing::CookedPak;
  using oxygen::cooker::testing::LooseCookedRoot;
  using oxygen::cooker::testing::Materialize;
  using oxygen::cooker::testing::SeedBytes;

  auto ReadMaterialDesc(const std::vector<std::byte>& bytes)
    -> data::pak::render::MaterialAssetDesc
  {
    return oxygen::serio::CopyAt<data::pak::render::MaterialAssetDesc>(
      std::span { bytes })
      .value_or(data::pak::render::MaterialAssetDesc {});
  }

  using oxygen::cooker::test::HasDiagnosticCode;

  //! The identity a material request is submitted under.
  struct MaterialIdentity final {
    std::string_view asset_virtual_path = "/Cooked/Materials/woodfloor007.omat";
    std::string_view job_name = "woodfloor007";
  };

  class MaterialDescriptorImportJobTest : public ImportServiceTest {
  protected:
    auto MakeRequest(const std::filesystem::path& cooked_root,
      std::string descriptor_json, const MaterialIdentity& identity = {})
      -> ImportRequest
    {
      auto request = ImportRequest {};
      request.source_path = "inline://woodfloor007.omat.json";
      request.source_mount_path = "/Content/woodfloor007.omat.json";
      request.job_name = std::string(identity.job_name);
      request.asset_virtual_path = std::string(identity.asset_virtual_path);
      request.cooked_root = cooked_root;
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
    const auto from = [&](std::string_view folder) -> ImportRequest {
      auto request = MakeRequest(cooked_root, R"({ "name": "woodfloor007" })");
      request.source_path = TempDir() / folder / "woodfloor007.omat.json";
      request.source_mount_path
        = "/Content/" + std::string(folder) + "/woodfloor007.omat.json";
      return request;
    };

    const auto first = Import(from("A"));
    const auto again = Import(from("A"));
    const auto other = Import(from("B"));

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
      = index.FindAssetKeyByVirtualPath("/Cooked/Materials/woodfloor007.omat");
    ASSERT_HAS_VALUE(key);
    EXPECT_EQ(index.FindSourcePath(*key),
      std::optional<std::string_view>("/Content/A/woodfloor007.omat.json"));
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
      {
        .asset_virtual_path = "/Cooked/Materials/HdrEmission.omat",
        .job_name = "HdrEmission",
      }));
    ASSERT_HAS_VALUE(report);
    ASSERT_TRUE(report->success);
    const auto bytes
      = ReadBytes(CookedFile(cooked_root, "Materials/HdrEmission.omat"));
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
    auto textures = LooseCookedRoot { "Cooked" };
    const auto texture_row = textures.AddTexture(SeedBytes(1U));
    textures.DescribeTexture(
      "Resources/Textures/woodfloor007_color.otex", texture_row);
    Materialize(textures.Files(), cooked_root);

    const auto report = Import(MakeRequest(cooked_root, R"({
      "name": "woodfloor007",
      "domain": "opaque",
      "textures": {
        "base_color": {
          "virtual_path": "../Resources/Textures/woodfloor007_color.otex"
        }
      }
    })"));

    ASSERT_HAS_VALUE(report);
    EXPECT_TRUE(report->success);
    EXPECT_EQ(report->materials_written, 1U);
    EXPECT_FALSE(HasDiagnosticCode(
      report->diagnostics, "material.descriptor.texture_descriptor_missing"));

    ASSERT_TRUE(HasCookedFile(cooked_root, "Materials/woodfloor007.omat"));
    const auto bytes
      = ReadBytes(CookedFile(cooked_root, "Materials/woodfloor007.omat"));
    ASSERT_GE(bytes.size(), sizeof(data::pak::render::MaterialAssetDesc));
    const auto desc = ReadMaterialDesc(bytes);
    EXPECT_EQ(desc.base_color_texture.get(), 0U);
    const auto index = lc::LooseCookedIndex::LoadFromRoot(cooked_root);
    const auto key
      = index.FindAssetKeyByVirtualPath("/Cooked/Materials/woodfloor007.omat");
    ASSERT_HAS_VALUE(key);
    const auto references = index.FindAssetReferences(*key);
    ASSERT_HAS_VALUE(references);
    EXPECT_THAT(references->Resources(),
      ::testing::ElementsAre(data::ResourceBinding {
        .kind = data::ResourceKind::kTexture,
        .index = texture_row,
      }));
  }

  //! A descriptor under another name, such as a hashed one, is not the
  //! texture: the reference is missing.
  NOLINT_TEST_F(
    MaterialDescriptorImportJobTest, IgnoresDescriptorsUnderAnotherName)
  {
    const auto cooked_root = TempDir() / "ignore_hashed_texture_name";
    std::filesystem::create_directories(cooked_root);
    auto textures = LooseCookedRoot { "Cooked" };
    const auto texture_row = textures.AddTexture(SeedBytes(1U));
    textures.DescribeTexture(
      "Resources/Textures/woodfloor007_color_1234567890abcdef.otex",
      texture_row);
    Materialize(textures.Files(), cooked_root);

    const auto report = Import(MakeRequest(cooked_root, R"({
      "name": "woodfloor007",
      "domain": "opaque",
      "textures": {
        "base_color": {
          "virtual_path": "../Resources/Textures/woodfloor007_color.otex"
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
          "virtual_path": "../Resources/Textures/woodfloor007_color.otex"
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
      = ReadBytes(CookedFile(cooked_root, "Materials/woodfloor007.omat"));
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
    auto library_root = LooseCookedRoot { "Library" };
    const auto grid_row = library_root.AddTexture(SeedBytes(1U));
    const auto kGrid
      = library_root.DescribeTexture("Textures/Grid.otex", grid_row);
    // A pak holds at least one asset: a material that uses the texture.
    auto grid_material = oxygen::cooker::testing::MaterialBuilder { "Grid" };
    grid_material.Texture(
      &oxygen::data::pak::render::MaterialAssetDesc::base_color_texture,
      grid_row);
    library_root.Material("Materials/Grid.omat", grid_material);
    const auto library = TempDir() / "library";
    Materialize(library_root.Files(), library);
    const auto pak = TempDir() / "library.pak";
    Materialize(CookedPak { library_root }.Encode(), pak);
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
      auto request = MakeRequest(cooked_root, kDescriptor,
        {
          .asset_virtual_path = "/Cooked/Materials/Tiles.omat",
          .job_name = "Tiles",
        });
      request.cooked_context_roots = { context };

      // Act
      const auto report = Import(std::move(request));

      // Assert
      ASSERT_HAS_VALUE(report);
      EXPECT_TRUE(report->success) << context;
      const auto desc = ReadMaterialDesc(
        ReadBytes(CookedFile(cooked_root, "Materials/Tiles.omat")));
      EXPECT_EQ(desc.base_color_texture.get(), 0U);
      EXPECT_EQ(desc.normal_texture.get(), 0U);
      const auto index = lc::LooseCookedIndex::LoadFromRoot(cooked_root);
      const auto key
        = index.FindAssetKeyByVirtualPath("/Cooked/Materials/Tiles.omat");
      ASSERT_HAS_VALUE(key);
      const auto references = index.FindAssetReferences(*key);
      ASSERT_HAS_VALUE(references);
      const auto target = references->ResolveTexture(desc.base_color_texture);
      ASSERT_HAS_VALUE(target);
      EXPECT_EQ(std::get<data::AssetKey>(*target),
        data::TextureKeyFromVirtualPath(kGrid));
    }
  }

} // namespace

} // namespace oxygen::content::import::test
