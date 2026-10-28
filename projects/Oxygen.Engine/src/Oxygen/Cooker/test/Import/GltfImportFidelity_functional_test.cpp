//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/gltf/GltfAdapter.cpp (UV sets, emissive strength)

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ModelImportState.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/ImportHarness.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {

  using nlohmann::json;
  using oxygen::cooker::test::DiagnosticSummary;
  using oxygen::cooker::test::HasDiagnosticCode;
  using oxygen::cooker::test::ImportServiceTest;
  using oxygen::cooker::test::ReadBytes;
  using oxygen::cooker::test::ReadText;
  using oxygen::cooker::test::WriteBytes;
  using oxygen::cooker::test::WriteText;

  constexpr auto kModelSurfaceIdentity
    = std::string_view { "0196f2a0-5b4c-7d3e-8f10-2a3b4c5d6e81" };

  class GltfImportFidelityTest : public ImportServiceTest {
  protected:
    //! Cooks `document`, written beside the fixture's checker image.
    auto Cook(const json& document, const std::string_view name)
      -> std::optional<ImportReport>
    {
      const auto fixture
        = oxygen::cooker::test::ModelPath("static_textured_checker.png");
      const auto source = TempPath("source") / name;
      WriteText(source, document.dump());
      WriteBytes(source.parent_path() / fixture.filename(), ReadBytes(fixture));
      const auto identity = Uuid::FromString(kModelSurfaceIdentity);
      if (!identity.has_value()) {
        ADD_FAILURE() << "invalid surface identity";
        return std::nullopt;
      }
      auto request = ImportRequest {};
      request.source_path = source;
      request.asset_virtual_path = "/.cooked/" + std::string(name);
      request.cooked_root = CookedRoot();
      request.surface_provenance
        = std::make_shared<const SurfaceProvenance>(*identity);
      request.model_import = ModelImportInputs {};
      return Import(std::move(request));
    }
  };

  //! A mesh keeps one UV set. A primitive with two, whose material's occlusion
  //! map reads the second, reports both losses: the dropped set, and the
  //! material's textures reading different sets.
  NOLINT_TEST_F(GltfImportFidelityTest, DroppedUvSetIsReported)
  {
    const auto fixture
      = oxygen::cooker::test::ModelPath("static_textured_triangle.gltf");
    auto document = json::parse(ReadText(fixture));
    auto& primitive = document["meshes"].at(0)["primitives"].at(0);
    primitive["attributes"]["TEXCOORD_1"]
      = primitive["attributes"]["TEXCOORD_0"];
    document["materials"].at(0)["occlusionTexture"]
      = { { "index", 0 }, { "texCoord", 1 } };

    const auto report = Cook(document, "two_uv_sets.gltf");

    ASSERT_HAS_VALUE(report);
    ASSERT_TRUE(report->success) << DiagnosticSummary(report->diagnostics);
    EXPECT_TRUE(HasDiagnosticCode(report->diagnostics, "mesh.uv_sets_dropped"))
      << DiagnosticSummary(report->diagnostics);
    EXPECT_TRUE(HasDiagnosticCode(report->diagnostics, "mesh.uv_set_conflict"))
      << DiagnosticSummary(report->diagnostics);
  }

  //! glTF emission is emissiveFactor times KHR_materials_emissive_strength;
  //! the cooked factor holds HDR emission, so it carries the product.
  NOLINT_TEST_F(GltfImportFidelityTest, EmissiveStrengthScalesTheEmission)
  {
    auto document = json::parse(ReadText(
      oxygen::cooker::test::ModelPath("static_textured_triangle.gltf")));
    document["extensionsUsed"] = { "KHR_materials_emissive_strength" };
    auto& material = document["materials"].at(0);
    material["emissiveFactor"] = { 1.0, 0.5, 0.25 };
    material["extensions"]["KHR_materials_emissive_strength"]
      = { { "emissiveStrength", 8.0 } };

    const auto report = Cook(document, "glow.gltf");

    ASSERT_HAS_VALUE(report);
    ASSERT_TRUE(report->success) << DiagnosticSummary(report->diagnostics);
    EXPECT_TRUE(std::ranges::none_of(report->diagnostics,
      [](const ImportDiagnostic& diagnostic) -> bool {
        return diagnostic.object_path
          == "/extensions/KHR_materials_emissive_strength";
      }))
      << DiagnosticSummary(report->diagnostics);
    const auto product = std::ranges::find(report->products,
      data::AssetType::kMaterial, &ImportProductRecord::asset_type);
    ASSERT_NE(product, report->products.end());
    const auto bytes
      = ReadBytes(report->cooked_root / product->descriptor_relpath);
    auto desc = data::pak::render::MaterialAssetDesc {};
    ASSERT_GE(bytes.size(), sizeof(desc));
    std::memcpy(&desc, bytes.data(), sizeof(desc));
    EXPECT_FLOAT_EQ(desc.emissive_factor[0], 8.0F);
    EXPECT_FLOAT_EQ(desc.emissive_factor[1], 4.0F);
    EXPECT_FLOAT_EQ(desc.emissive_factor[2], 2.0F);
  }

} // namespace
} // namespace oxygen::content::import::test
