//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/MaterialSource.cpp

#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/MaterialSource.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Core/Types/TextureSampling.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {
  using oxygen::cooker::test::HasDiagnosticCode;
  using oxygen::cooker::test::MaterialDescriptor;

  NOLINT_TEST(
    MaterialSourceTest, PreparesEveryNativeTextureSlotWithoutReadingFiles)
  {
    auto document
      = MaterialDescriptor { "/Content/Materials/NativeMaterial.omat" };
    for (const auto& slot : MaterialSource::TextureSlots()) {
      const auto name = std::string(slot.name);
      document.Texture(name, "/Content/Textures/" + name + ".otex")
        .Set("/textures/" + name + "/uv_set", 1)
        .Set("/textures/" + name + "/uv_transform",
          {
            { "scale", { 2.0F, 3.0F } },
            { "offset", { 0.1F, 0.2F } },
            { "rotation_radians", 0.5F },
          });
    }
    std::vector<ImportDiagnostic> diagnostics;
    const auto source = MaterialSource::FromDescriptor(
      document.Text(), "source.omat.json", "", diagnostics);
    ASSERT_HAS_VALUE(source) << "Expected source to contain a value";
    EXPECT_TRUE(diagnostics.empty());
    EXPECT_EQ(MaterialSource::TextureSlots().size(), 12U);
    for (const auto& slot : MaterialSource::TextureSlots()) {
      const auto& binding = source->textures.*slot.binding;
      EXPECT_TRUE(binding.assigned);
      EXPECT_EQ(binding.source_id,
        document.Spell(
          "/Content/Textures/" + std::string(slot.name) + ".otex"));
      EXPECT_EQ(binding.uv_set, 1U);
      EXPECT_EQ(binding.uv_transform.scale[0], 2.0F);
      EXPECT_EQ(binding.uv_transform.offset[1], 0.2F);
      EXPECT_EQ(binding.uv_transform.rotation_radians, 0.5F);
    }
  }

  NOLINT_TEST(MaterialSourceTest, PreservesNameRecipePrecedence)
  {
    std::vector<ImportDiagnostic> diagnostics;
    const auto named = MaterialSource::FromDescriptor(
      MaterialDescriptor { "/Content/Materials/Authored.omat" }.Text(),
      "Fallback.omat.json", "Override", diagnostics);
    ASSERT_HAS_VALUE(named) << "Expected named to contain a value";
    EXPECT_EQ(named->name, "Override");
    EXPECT_EQ(named->storage_name, "Override");
    const auto authored = MaterialSource::FromDescriptor(
      MaterialDescriptor { "/Content/Materials/Authored.omat" }.Text(),
      "Fallback.omat.json", "", diagnostics);
    ASSERT_HAS_VALUE(authored) << "Expected authored to contain a value";
    EXPECT_EQ(authored->name, "Authored");
    const auto fallback = MaterialSource::FromDescriptor(
      MaterialDescriptor { "/Content/Materials/Authored.omat" }
        .Remove("/name")
        .Text(),
      "Fallback.omat.json", "", diagnostics);
    ASSERT_HAS_VALUE(fallback) << "Expected fallback to contain a value";
    EXPECT_EQ(fallback->name, "Fallback.omat");
  }

  NOLINT_TEST(MaterialSourceTest, PreparationRejectsDuplicateShaderStages)
  {
    std::vector<ImportDiagnostic> diagnostics;
    const auto result = MaterialSource::FromDescriptor(
      MaterialDescriptor { "/Content/Materials/Shaded.omat" }
        .Set("/shaders", nlohmann::json::parse(R"([
      {"stage":"pixel","source_path":"declared.hlsl","entry_point":"PS"},
      {"stage":"pixel","source_path":"another.hlsl","entry_point":"PS"}])"))
        .Text(),
      "material.json", "", diagnostics);
    EXPECT_FALSE(result);
    EXPECT_TRUE(HasDiagnosticCode(
      diagnostics, import_codes::kMaterialShaderStageDuplicate));
  }

  NOLINT_TEST(MaterialSourceTest, PreparationRejectsShadersTheRuntimeRefuses)
  {
    for (const auto* shader :
      {
        R"([{"stage":"pixel","source_path":"lit.hlsl","entry_point":"2PS"}])",
        R"([{"stage":"pixel","source_path":"../lit.hlsl","entry_point":"PS"}])",
        R"([{"stage":"pixel","source_path":"C:/lit.hlsl","entry_point":"PS"}])",
        R"([{"stage":"pixel","source_path":"lit.hlsl","entry_point":"PS",
          "defines":"ALPHA,ALPHA=1"}])",
        R"([{"stage":"pixel","source_path":"lit.hlsl","entry_point":"PS",
          "defines":"alpha"}])",
      }) {
      SCOPED_TRACE(shader);
      std::vector<ImportDiagnostic> diagnostics;
      const auto result = MaterialSource::FromDescriptor(
        MaterialDescriptor { "/Content/Materials/Shaded.omat" }
          .Set("/shaders", nlohmann::json::parse(shader))
          .Text(),
        "material.json", "", diagnostics);
      EXPECT_FALSE(result);
      EXPECT_TRUE(HasDiagnosticCode(
        diagnostics, import_codes::kMaterialShaderRefInvalid));
    }
  }

  NOLINT_TEST(MaterialSourceTest, PreparationCanonicalizesShaderRequests)
  {
    std::vector<ImportDiagnostic> diagnostics;
    const auto result = MaterialSource::FromDescriptor(
      MaterialDescriptor { "/Content/Materials/Shaded.omat" }
        .Set("/shaders", nlohmann::json::parse(R"([
      {"stage":"pixel","source_path":"./Lit\\Common/../Lit_PS.hlsl",
       "entry_point":"PS","defines":"LOD=2,ALPHA"}])"))
        .Text(),
      "material.json", "", diagnostics);
    ASSERT_HAS_VALUE(result) << "Expected result to contain a value";
    ASSERT_EQ(result->shader_requests.size(), 1U);
    EXPECT_EQ(result->shader_requests.front().source_path, "Lit/Lit_PS.hlsl");
    EXPECT_EQ(result->shader_requests.front().defines, "ALPHA,LOD=2");
  }

  NOLINT_TEST(MaterialSourceTest, PreparationRejectsUnsupportedEmissionRange)
  {
    std::vector<ImportDiagnostic> diagnostics;
    const auto result = MaterialSource::FromDescriptor(
      MaterialDescriptor { "/Content/Materials/Glow.omat" }
        .Set("/parameters",
          {
            { "emissive_color", { 1, 1, 1 } },
            { "emissive_intensity", 70000 },
          })
        .Text(),
      "material.json", "", diagnostics);
    EXPECT_FALSE(result);
    EXPECT_TRUE(HasDiagnosticCode(diagnostics,
                  import_codes::kMaterialDescriptorSchemaValidationFailed)
      || HasDiagnosticCode(
        diagnostics, import_codes::kMaterialEmissiveFactorRange));
  }

  //! Every engine capability is authorable: the grid and how ambient occlusion
  //! combines with its texture.
  NOLINT_TEST(MaterialSourceTest, ReadsProceduralGridAndOcclusionMode)
  {
    std::vector<ImportDiagnostic> diagnostics;
    const auto result = MaterialSource::FromDescriptor(
      MaterialDescriptor { "/Content/Materials/Floor.omat" }
        .Set("/ambient_occlusion_mode", "strength")
        .Set("/procedural_grid/spacing", { 2.0, 3.0 })
        .Set("/procedural_grid/major_every", 5)
        .Set("/procedural_grid/fade_start", 10.0)
        .Set("/procedural_grid/fade_end", 20.0)
        .Set("/procedural_grid/origin_color", { 0.1, 0.2, 0.3, 0.4 })
        .Text(),
      "material.json", "", diagnostics);

    ASSERT_HAS_VALUE(result);
    EXPECT_EQ(result->occlusion_mode, AmbientOcclusionMode::kStrength);
    ASSERT_HAS_VALUE(result->procedural_grid);
    EXPECT_FLOAT_EQ(result->procedural_grid->spacing[1], 3.0F);
    EXPECT_EQ(result->procedural_grid->major_every, 5U);
    EXPECT_FLOAT_EQ(result->procedural_grid->fade_end, 20.0F);
    EXPECT_FLOAT_EQ(result->procedural_grid->origin_color[3], 0.4F);
    EXPECT_FLOAT_EQ(result->procedural_grid->major_thickness, 0.04F);
  }

  //! An empty grid draws thin lines: a minor line as wide as the spacing would
  //! fill the surface with its color.
  NOLINT_TEST(MaterialSourceTest, EmptyGridHasLinesThinnerThanItsSpacing)
  {
    std::vector<ImportDiagnostic> diagnostics;
    const auto result = MaterialSource::FromDescriptor(
      MaterialDescriptor { "/Content/Materials/Floor.omat" }
        .Set("/procedural_grid", nlohmann::json::object())
        .Text(),
      "material.json", "", diagnostics);

    ASSERT_HAS_VALUE(result);
    ASSERT_HAS_VALUE(result->procedural_grid);
    const auto& grid = *result->procedural_grid;
    EXPECT_FLOAT_EQ(grid.line_thickness, 0.02F);
    EXPECT_FLOAT_EQ(grid.major_thickness, 0.04F);
    EXPECT_FLOAT_EQ(grid.axis_thickness, 0.06F);
    EXPECT_LT(grid.line_thickness, grid.spacing[0]);
  }

  NOLINT_TEST(MaterialSourceTest, GridFadeMustEndAfterItStarts)
  {
    std::vector<ImportDiagnostic> diagnostics;
    const auto result = MaterialSource::FromDescriptor(
      MaterialDescriptor { "/Content/Materials/Floor.omat" }
        .Set("/procedural_grid/fade_start", 20.0)
        .Set("/procedural_grid/fade_end", 10.0)
        .Text(),
      "material.json", "", diagnostics);

    EXPECT_FALSE(result);
    EXPECT_TRUE(
      HasDiagnosticCode(diagnostics, import_codes::kMaterialGridInvalid));
  }

  //! A domain says how a material is transparent when no alpha mode does: only
  //! a stated mode can disagree with it.
  NOLINT_TEST(MaterialSourceTest, OmittedAlphaModeFollowsTheDomain)
  {
    const auto mode_of = [](const char* domain) -> MaterialAlphaMode {
      std::vector<ImportDiagnostic> diagnostics;
      const auto result = MaterialSource::FromDescriptor(
        MaterialDescriptor { "/Content/Materials/Mat.omat" }
          .Set("/domain", domain)
          .Text(),
        "material.json", "", diagnostics);
      return result.has_value() ? result->alpha_mode
                                : MaterialAlphaMode::kOpaque;
    };
    EXPECT_EQ(mode_of("masked"), MaterialAlphaMode::kMasked);
    EXPECT_EQ(mode_of("alpha_blended"), MaterialAlphaMode::kBlended);
    EXPECT_EQ(mode_of("opaque"), MaterialAlphaMode::kOpaque);
  }

  //! Through the API a value that is not finite is refused; from a model file
  //! it takes its default, so the material still imports.
  NOLINT_TEST(MaterialSourceTest, NonFiniteValuesAreRefusedOrRepaired)
  {
    constexpr auto kNan = std::numeric_limits<float>::quiet_NaN();
    auto material = MaterialSource {};
    material.name = "Mat";
    material.inputs.normal_scale = kNan;
    material.inputs.base_color[1] = kNan;
    material.textures.normal.uv_transform.rotation_radians
      = std::numeric_limits<float>::infinity();

    std::vector<ImportDiagnostic> refused;
    EXPECT_FALSE(material.Validate("material", refused));
    EXPECT_TRUE(
      HasDiagnosticCode(refused, import_codes::kMaterialInputNotFinite));

    std::vector<ImportDiagnostic> repaired;
    material.RepairModelInputs("material", repaired);
    EXPECT_TRUE(
      HasDiagnosticCode(repaired, import_codes::kMaterialInputNotFinite));
    EXPECT_FLOAT_EQ(material.inputs.normal_scale, 1.0F);
    EXPECT_FLOAT_EQ(material.inputs.base_color[1], 1.0F);
    EXPECT_FLOAT_EQ(
      material.textures.normal.uv_transform.rotation_radians, 0.0F);
    std::vector<ImportDiagnostic> valid;
    EXPECT_TRUE(material.Validate("material", valid));
  }

  //! A model file naming a set vertices do not hold reads the first, and hears
  //! so; a descriptor cannot name one, since validation refuses it.
  NOLINT_TEST(MaterialSourceTest, ModelBindingPastTheSetsVerticesHoldIsRepaired)
  {
    auto material = MaterialSource {};
    material.textures.normal.assigned = true;
    material.textures.normal.uv_set = 2;
    auto refused = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(material.Validate("material", refused));
    EXPECT_TRUE(HasDiagnosticCode(
      refused, import_codes::kMaterialDescriptorTextureUvInvalid));

    auto diagnostics = std::vector<ImportDiagnostic> {};
    material.RepairModelInputs("material", diagnostics);

    EXPECT_TRUE(
      HasDiagnosticCode(diagnostics, import_codes::kMaterialUvDropped));
    EXPECT_EQ(material.textures.normal.uv_set, 0U);
    auto valid = std::vector<ImportDiagnostic> {};
    EXPECT_TRUE(material.Validate("material", valid));
  }

  NOLINT_TEST(MaterialSourceTest, DescriptorKeepsEachBindingsSampler)
  {
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto result = MaterialSource::FromDescriptor(
      MaterialDescriptor { "/Content/Materials/Label.omat" }
        .Texture("base_color", "/Content/label.otex")
        .Set("/textures/base_color/sampler/address_u", "clamp")
        .Set("/textures/base_color/sampler/address_v", "mirror")
        .Set("/textures/base_color/sampler/mag_filter", "nearest")
        .Set("/textures/base_color/sampler/min_filter", "nearest")
        .Set("/textures/base_color/sampler/mip_filter", "none")
        .Set("/textures/base_color/uv_set", 1)
        .Texture("normal", "/Content/label_n.otex")
        .Text(),
      "material.json", "", diagnostics);

    ASSERT_HAS_VALUE(result);
    EXPECT_EQ(result->textures.base_color.uv_set, 1U);
    EXPECT_EQ(result->textures.base_color.sampler,
      (TextureSamplerState {
        .address_u = TextureAddress::kClamp,
        .address_v = TextureAddress::kMirror,
        .mag_filter = TextureFilter::kNearest,
        .min_filter = TextureFilter::kNearest,
        .mip_filter = TextureMipFilter::kNone,
      }));
    EXPECT_EQ(result->textures.normal.sampler, TextureSamplerState {});
  }

  //! One texture packing two channels is sampled one way.
  NOLINT_TEST(MaterialSourceTest, ForcePackedRequiresTheSameSampler)
  {
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto result = MaterialSource::FromDescriptor(
      MaterialDescriptor { "/Content/Materials/Packed.omat" }
        .Set("/orm_policy", "force_packed")
        .Texture("metallic", "/Content/orm.otex")
        .Texture("roughness", "/Content/orm.otex")
        .Set("/textures/roughness/sampler/address_u", "clamp")
        .Text(),
      "material.json", "", diagnostics);
    EXPECT_FALSE(result);
    EXPECT_TRUE(
      HasDiagnosticCode(diagnostics, import_codes::kMaterialOrmPolicy));
  }

  NOLINT_TEST(MaterialSourceTest, ForcePackedRequiresTheSameTextureAndUv)
  {
    std::vector<ImportDiagnostic> diagnostics;
    const auto result = MaterialSource::FromDescriptor(
      MaterialDescriptor { "/Content/Materials/Packed.omat" }
        .Set("/orm_policy", "force_packed")
        .Texture("metallic", "/Content/orm.otex")
        .Texture("roughness", "/Content/orm.otex")
        .Set("/textures/roughness/uv_set", 1)
        .Text(),
      "material.json", "", diagnostics);
    EXPECT_FALSE(result);
    EXPECT_TRUE(
      HasDiagnosticCode(diagnostics, import_codes::kMaterialOrmPolicy));
  }

  NOLINT_TEST(
    MaterialSourceTest, ValidatesModelAdapterValuesWithoutDescriptorParsing)
  {
    MaterialSource source;
    source.name = "Imported";
    source.inputs.emissive_factor[0] = std::numeric_limits<float>::infinity();
    std::vector<ImportDiagnostic> diagnostics;
    EXPECT_FALSE(source.Validate("model.gltf", diagnostics));
    EXPECT_TRUE(HasDiagnosticCode(
      diagnostics, import_codes::kMaterialEmissiveFactorRange));
  }
} // namespace
} // namespace oxygen::content::import::test
