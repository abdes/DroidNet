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

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/MaterialSource.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
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
    EXPECT_TRUE(
      HasDiagnosticCode(diagnostics, "material.shader_stage_duplicate"));
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
      EXPECT_TRUE(
        HasDiagnosticCode(diagnostics, "material.shader_ref_invalid"));
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
    EXPECT_TRUE(HasDiagnosticCode(
                  diagnostics, "material.descriptor.schema_validation_failed")
      || HasDiagnosticCode(diagnostics, "material.emissive_factor_range"));
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
    EXPECT_TRUE(HasDiagnosticCode(diagnostics, "material.orm_policy"));
  }

  NOLINT_TEST(
    MaterialSourceTest, ValidatesModelAdapterValuesWithoutDescriptorParsing)
  {
    MaterialSource source;
    source.name = "Imported";
    source.inputs.emissive_factor[0] = std::numeric_limits<float>::infinity();
    std::vector<ImportDiagnostic> diagnostics;
    EXPECT_FALSE(source.Validate("model.gltf", diagnostics));
    EXPECT_TRUE(
      HasDiagnosticCode(diagnostics, "material.emissive_factor_range"));
  }
} // namespace
} // namespace oxygen::content::import::test
