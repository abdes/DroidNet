//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/gltf/GltfAdapter.cpp (UV sets, emissive strength)

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ModelImportState.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Cooker/test/Support/CookedRecords.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/ImportHarness.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Core/Types/TextureSampling.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/MaterialTextureSlot.h>
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
      request.asset_virtual_path = "/Cooked/" + std::string(name);
      request.source_mount_path
        = oxygen::cooker::test::SourceMountPath(request.source_path);
      request.cooked_root = CookedRoot();
      request.surface_provenance
        = std::make_shared<const SurfaceProvenance>(*identity);
      request.model_import = ModelImportInputs {};
      return Import(std::move(request));
    }
  };

  //! A mesh keeps two UV sets: a primitive with two loses neither.
  NOLINT_TEST_F(GltfImportFidelityTest, SecondUvSetIsKept)
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
    EXPECT_FALSE(
      HasDiagnosticCode(report->diagnostics, import_codes::kMeshUvSetsDropped))
      << DiagnosticSummary(report->diagnostics);
    EXPECT_FALSE(
      HasDiagnosticCode(report->diagnostics, import_codes::kMeshUvSetConflict))
      << DiagnosticSummary(report->diagnostics);
    EXPECT_FALSE(
      HasDiagnosticCode(report->diagnostics, import_codes::kMeshUvSetMissing))
      << DiagnosticSummary(report->diagnostics);
  }

  //! A set past the two a mesh keeps is reported as lost.
  NOLINT_TEST_F(GltfImportFidelityTest, ThirdUvSetIsReported)
  {
    const auto fixture
      = oxygen::cooker::test::ModelPath("static_textured_triangle.gltf");
    auto document = json::parse(ReadText(fixture));
    auto& primitive = document["meshes"].at(0)["primitives"].at(0);
    primitive["attributes"]["TEXCOORD_1"]
      = primitive["attributes"]["TEXCOORD_0"];
    primitive["attributes"]["TEXCOORD_2"]
      = primitive["attributes"]["TEXCOORD_0"];

    const auto report = Cook(document, "three_uv_sets.gltf");

    ASSERT_HAS_VALUE(report);
    ASSERT_TRUE(report->success) << DiagnosticSummary(report->diagnostics);
    EXPECT_TRUE(
      HasDiagnosticCode(report->diagnostics, import_codes::kMeshUvSetsDropped))
      << DiagnosticSummary(report->diagnostics);
  }

  //! A texture of a set the primitive lacks reads its first, and says so.
  NOLINT_TEST_F(GltfImportFidelityTest, MissingUvSetIsReported)
  {
    const auto fixture
      = oxygen::cooker::test::ModelPath("static_textured_triangle.gltf");
    auto document = json::parse(ReadText(fixture));
    document["materials"].at(0)["occlusionTexture"]
      = { { "index", 0 }, { "texCoord", 1 } };

    const auto report = Cook(document, "missing_uv_set.gltf");

    ASSERT_HAS_VALUE(report);
    ASSERT_TRUE(report->success) << DiagnosticSummary(report->diagnostics);
    EXPECT_TRUE(
      HasDiagnosticCode(report->diagnostics, import_codes::kMeshUvSetMissing))
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
    const auto desc = oxygen::cooker::test::ReadMaterialDesc(bytes);
    EXPECT_FLOAT_EQ(desc.emissive_factor[0], 8.0F);
    EXPECT_FLOAT_EQ(desc.emissive_factor[1], 4.0F);
    EXPECT_FLOAT_EQ(desc.emissive_factor[2], 2.0F);
  }

  //! glTF's sampler vocabulary, one material per case: its min filter holds
  //! both the min and the mip filter, and a filter it leaves out is linear.
  NOLINT_TEST_F(GltfImportFidelityTest, EachMaterialKeepsItsSampler)
  {
    struct Case {
      json sampler;
      TextureSamplerState expected;
    };
    const auto cases = std::array {
      Case { .sampler=json::object(), .expected=TextureSamplerState {} },
      Case {
        .sampler={ { "wrapS", 33071 }, { "wrapT", 33648 } },
        .expected={
          .address_u = TextureAddress::kClamp,
          .address_v = TextureAddress::kMirror,
        },
      },
      Case {
        .sampler={ { "magFilter", 9728 }, { "minFilter", 9728 } },
        .expected={
          .mag_filter = TextureFilter::kNearest,
          .min_filter = TextureFilter::kNearest,
          .mip_filter = TextureMipFilter::kNone,
        },
      },
      Case {
        .sampler={ { "magFilter", 9729 }, { "minFilter", 9729 } },
        .expected={ .mip_filter = TextureMipFilter::kNone },
      },
      Case {
        .sampler={ { "minFilter", 9984 } },
        .expected={
          .min_filter = TextureFilter::kNearest,
          .mip_filter = TextureMipFilter::kNearest,
        },
      },
      Case {
        .sampler={ { "minFilter", 9985 } },
        .expected={ .mip_filter = TextureMipFilter::kNearest },
      },
      Case {
        .sampler={ { "minFilter", 9986 } },
        .expected={ .min_filter = TextureFilter::kNearest },
      },
      Case { .sampler={ { "minFilter", 9987 } }, .expected=TextureSamplerState {} },
    };
    auto document = json::parse(ReadText(
      oxygen::cooker::test::ModelPath("static_textured_triangle.gltf")));
    const auto material = document["materials"].at(0);
    const auto primitive = document["meshes"].at(0)["primitives"].at(0);
    document["materials"] = json::array();
    document["meshes"].at(0)["primitives"] = json::array();
    document["samplers"] = json::array();
    document["textures"] = json::array();
    for (std::size_t index = 0; index < cases.size(); ++index) {
      document["samplers"].push_back(cases.at(index).sampler);
      document["textures"].push_back({ { "source", 0 }, { "sampler", index } });
      auto authored = material;
      authored["name"] = "Case" + std::to_string(index);
      authored["pbrMetallicRoughness"]["baseColorTexture"]
        = { { "index", index } };
      document["materials"].push_back(authored);
      auto drawn = primitive;
      drawn["material"] = index;
      document["meshes"].at(0)["primitives"].push_back(drawn);
    }

    const auto report = Cook(document, "samplers.gltf");

    ASSERT_HAS_VALUE(report);
    ASSERT_TRUE(report->success) << DiagnosticSummary(report->diagnostics);
    EXPECT_FALSE(HasDiagnosticCode(
      report->diagnostics, import_codes::kSourceFeatureDropped))
      << DiagnosticSummary(report->diagnostics);
    // Eight samplers over one image are one texture, beside the fallback row.
    const auto content = content::cooked::CookedContent::Open(
      content::cooked::RealCookerStorage(), report->cooked_root);
    EXPECT_EQ(
      content.ResourceTable(data::ResourceKind::kTexture).RowCount(), 2U);
    for (std::size_t index = 0; index < cases.size(); ++index) {
      SCOPED_TRACE(index);
      const auto name = "Case" + std::to_string(index);
      const auto product = std::ranges::find_if(
        report->products, [&name](const ImportProductRecord& record) -> bool {
          return record.asset_type == data::AssetType::kMaterial
            && record.descriptor_relpath.contains(name);
        });
      ASSERT_NE(product, report->products.end());
      const auto desc = oxygen::cooker::test::ReadMaterialDesc(
        ReadBytes(report->cooked_root / product->descriptor_relpath));
      const auto& sampling = desc.texture_sampling[static_cast<std::size_t>(
        data::MaterialTextureSlot::kBaseColor)];
      const auto& expected = cases.at(index).expected;
      EXPECT_EQ(sampling.address_u, static_cast<uint8_t>(expected.address_u));
      EXPECT_EQ(sampling.address_v, static_cast<uint8_t>(expected.address_v));
      EXPECT_EQ(sampling.mag_filter, static_cast<uint8_t>(expected.mag_filter));
      EXPECT_EQ(sampling.min_filter, static_cast<uint8_t>(expected.min_filter));
      EXPECT_EQ(sampling.mip_filter, static_cast<uint8_t>(expected.mip_filter));
    }
  }

  //! Each texture of a material keeps its own KHR_texture_transform and UV set.
  NOLINT_TEST_F(GltfImportFidelityTest, EachTextureKeepsItsTransform)
  {
    auto document = json::parse(ReadText(
      oxygen::cooker::test::ModelPath("static_textured_triangle.gltf")));
    document["extensionsUsed"] = { "KHR_texture_transform" };
    auto& primitive = document["meshes"].at(0)["primitives"].at(0);
    primitive["attributes"]["TEXCOORD_1"]
      = primitive["attributes"]["TEXCOORD_0"];
    auto& material = document["materials"].at(0);
    material["pbrMetallicRoughness"]["baseColorTexture"] = {
      { "index", 0 },
      {
        "extensions",
        {
          {
            "KHR_texture_transform",
            {
              { "offset", { 0.25, 0.5 } },
              { "scale", { 2.0, 3.0 } },
              { "rotation", 0.5 },
            },
          },
        },
      },
    };
    material["occlusionTexture"] = { { "index", 0 }, { "texCoord", 1 } };

    const auto report = Cook(document, "transforms.gltf");

    ASSERT_HAS_VALUE(report);
    ASSERT_TRUE(report->success) << DiagnosticSummary(report->diagnostics);
    EXPECT_FALSE(HasDiagnosticCode(
      report->diagnostics, import_codes::kSourceFeatureDropped))
      << DiagnosticSummary(report->diagnostics);
    const auto product = std::ranges::find(report->products,
      data::AssetType::kMaterial, &ImportProductRecord::asset_type);
    ASSERT_NE(product, report->products.end());
    const auto desc = oxygen::cooker::test::ReadMaterialDesc(
      ReadBytes(report->cooked_root / product->descriptor_relpath));
    const auto& base = desc.texture_sampling[static_cast<std::size_t>(
      data::MaterialTextureSlot::kBaseColor)];
    EXPECT_EQ(base.uv_set, 0U);
    EXPECT_FLOAT_EQ(base.uv_scale[0], 2.0F);
    EXPECT_FLOAT_EQ(base.uv_scale[1], 3.0F);
    EXPECT_FLOAT_EQ(base.uv_offset[0], 0.25F);
    EXPECT_FLOAT_EQ(base.uv_offset[1], 0.5F);
    EXPECT_FLOAT_EQ(base.uv_rotation_radians, 0.5F);
    const auto& occlusion = desc.texture_sampling[static_cast<std::size_t>(
      data::MaterialTextureSlot::kAmbientOcclusion)];
    EXPECT_EQ(occlusion.uv_set, 1U);
    EXPECT_FLOAT_EQ(occlusion.uv_scale[0], 1.0F);
    EXPECT_FLOAT_EQ(occlusion.uv_offset[0], 0.0F);
  }

  //! A set past the two vertices hold reads the first, and the material hears.
  NOLINT_TEST_F(GltfImportFidelityTest, BindingToAThirdUvSetReadsTheFirst)
  {
    auto document = json::parse(ReadText(
      oxygen::cooker::test::ModelPath("static_textured_triangle.gltf")));
    auto& primitive = document["meshes"].at(0)["primitives"].at(0);
    primitive["attributes"]["TEXCOORD_1"]
      = primitive["attributes"]["TEXCOORD_0"];
    primitive["attributes"]["TEXCOORD_2"]
      = primitive["attributes"]["TEXCOORD_0"];
    document["materials"].at(0)["occlusionTexture"]
      = { { "index", 0 }, { "texCoord", 2 } };

    const auto report = Cook(document, "third_set_binding.gltf");

    ASSERT_HAS_VALUE(report);
    ASSERT_TRUE(report->success) << DiagnosticSummary(report->diagnostics);
    EXPECT_TRUE(
      HasDiagnosticCode(report->diagnostics, import_codes::kMaterialUvDropped))
      << DiagnosticSummary(report->diagnostics);
    const auto product = std::ranges::find(report->products,
      data::AssetType::kMaterial, &ImportProductRecord::asset_type);
    ASSERT_NE(product, report->products.end());
    const auto desc = oxygen::cooker::test::ReadMaterialDesc(
      ReadBytes(report->cooked_root / product->descriptor_relpath));
    EXPECT_EQ(desc
                .texture_sampling[static_cast<std::size_t>(
                  data::MaterialTextureSlot::kAmbientOcclusion)]
                .uv_set,
      0U);
  }

} // namespace
} // namespace oxygen::content::import::test
