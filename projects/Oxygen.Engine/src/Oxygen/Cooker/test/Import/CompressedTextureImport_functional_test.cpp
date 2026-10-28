//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/gltf/GltfAdapter.cpp (EXT_texture_webp,
//   KHR_texture_basisu), Import/Internal/ImageDecode.cpp,
//   Import/ImportRequest.cpp (texture formats)

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ModelImportState.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/ImportHarness.h>
#include <Oxygen/Cooker/test/Support/TestImages.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {

  using nlohmann::json;
  using oxygen::cooker::test::DiagnosticSummary;
  using oxygen::cooker::test::HasDiagnosticCode;
  using oxygen::cooker::test::ImportServiceTest;
  using oxygen::cooker::test::Ktx2Image;
  using oxygen::cooker::test::MakeKtx2;
  using oxygen::cooker::test::MakeWebp;
  using oxygen::cooker::test::ReadText;
  using oxygen::cooker::test::WriteBytes;
  using oxygen::cooker::test::WriteText;

  constexpr auto kSize = uint32_t { 8U };
  constexpr auto kModelSurfaceIdentity
    = std::string_view { "0196f2a0-5b4c-7d3e-8f10-2a3b4c5d6e80" };
  constexpr auto kPlaceholderUsed
    = std::string_view { "texture.placeholder_used" };

  //! An opaque mid-grey 8x8 RGBA8 image.
  [[nodiscard]] auto Grey() -> std::vector<std::byte>
  {
    constexpr auto kGrey = std::byte { 128U };
    return std::vector<std::byte>(std::size_t { kSize } * kSize * 4U, kGrey);
  }

  [[nodiscard]] auto Webp() -> std::vector<std::byte>
  {
    return MakeWebp(kSize, kSize, Grey());
  }

  [[nodiscard]] auto Ktx2() -> std::vector<std::byte>
  {
    const auto pixels = Grey();
    return MakeKtx2(Ktx2Image {
      .width = kSize,
      .height = kSize,
      .rgba = pixels,
    });
  }

  class CompressedTextureImportTest : public ImportServiceTest {
  protected:
    //! A texture-only request for `file`, written into the source folder.
    [[nodiscard]] auto TextureRequest(const std::string_view file,
      const std::vector<std::byte>& bytes) const -> ImportRequest
    {
      const auto source = TempPath("source") / file;
      WriteBytes(source, bytes);
      auto request = ImportRequest {};
      request.source_path = source;
      request.cooked_root = CookedRoot();
      request.loose_cooked_layout.virtual_mount_root = "/.cooked";
      request.asset_virtual_path = "/.cooked/Textures/"
        + std::filesystem::path(file).stem().string() + ".otex";
      return request;
    }
  };

  //! A glTF model whose base colour texture is WebP and whose normal texture
  //! is KTX2 (Basis Universal), both required extensions with no core image,
  //! cooks both textures from their images, with no placeholder.
  NOLINT_TEST_F(CompressedTextureImportTest, ModelCooksWebpAndKtx2Textures)
  {
    auto document = json::parse(
      ReadText(oxygen::cooker::test::ModelPath("static_scalar_triangle.gltf")));
    document["extensionsUsed"] = { "EXT_texture_webp", "KHR_texture_basisu" };
    document["extensionsRequired"]
      = { "EXT_texture_webp", "KHR_texture_basisu" };
    document["images"] = json::array({
      { { "uri", "color.webp" } },
      { { "uri", "normal.ktx2" } },
    });
    document["textures"] = json::array({
      { { "extensions", { { "EXT_texture_webp", { { "source", 0 } } } } } },
      { { "extensions", { { "KHR_texture_basisu", { { "source", 1 } } } } } },
    });
    auto& material = document["materials"].at(0);
    material["pbrMetallicRoughness"]["baseColorTexture"] = { { "index", 0 } };
    material["normalTexture"] = { { "index", 1 } };
    const auto source = TempPath("source/model.gltf");
    WriteText(source, document.dump());
    WriteBytes(source.parent_path() / "color.webp", Webp());
    WriteBytes(source.parent_path() / "normal.ktx2", Ktx2());
    auto request = ImportRequest {};
    request.source_path = source;
    request.asset_virtual_path = "/.cooked/model.gltf";
    request.cooked_root = CookedRoot();
    const auto identity = Uuid::FromString(kModelSurfaceIdentity);
    ASSERT_HAS_VALUE(identity);
    request.surface_provenance
      = std::make_shared<const SurfaceProvenance>(*identity);
    request.model_import = ModelImportInputs {};

    const auto report = Import(std::move(request));

    ASSERT_HAS_VALUE(report);
    ASSERT_TRUE(report->success) << DiagnosticSummary(report->diagnostics);
    EXPECT_FALSE(HasDiagnosticCode(report->diagnostics, kPlaceholderUsed))
      << DiagnosticSummary(report->diagnostics);
    // A texture descriptor is the one product without an asset key.
    EXPECT_EQ(std::ranges::count_if(report->products,
                [](const ImportProductRecord& product) -> bool {
                  return !product.key.has_value();
                }),
      2);
  }

  //! Standalone WebP and KTX2 files are texture sources.
  NOLINT_TEST_F(CompressedTextureImportTest, WebpAndKtx2FilesImportAsTextures)
  {
    for (auto request : {
           TextureRequest("albedo.webp", Webp()),
           TextureRequest("detail.ktx2", Ktx2()),
         }) {
      EXPECT_EQ(request.GetFormat(), ImportFormat::kTextureImage);
      const auto source = request.source_path;

      const auto report = Import(std::move(request));

      ASSERT_HAS_VALUE(report);
      EXPECT_TRUE(report->success)
        << source << ": " << DiagnosticSummary(report->diagnostics);
      EXPECT_FALSE(HasDiagnosticCode(report->diagnostics, kPlaceholderUsed))
        << source << ": " << DiagnosticSummary(report->diagnostics);
    }
  }

} // namespace
} // namespace oxygen::content::import::test
