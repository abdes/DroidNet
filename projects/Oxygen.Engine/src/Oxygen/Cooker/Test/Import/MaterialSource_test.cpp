//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/MaterialSource.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {
  auto HasCode(const std::vector<ImportDiagnostic>& diagnostics,
    const std::string_view code) -> bool
  {
    return std::ranges::any_of(
      diagnostics, [code](const auto& item) { return item.code == code; });
  }

  NOLINT_TEST(
    MaterialSourceTest, PreparesEveryNativeTextureSlotWithoutReadingFiles)
  {
    auto document = nlohmann::json { { "name", "NativeMaterial" } };
    auto textures = nlohmann::json::object();
    for (const auto& slot : MaterialSource::TextureSlots()) {
      textures.emplace(slot.name,
        nlohmann::json {
          { "virtual_path",
            "/Content/Textures/" + std::string(slot.name) + ".otex" },
          { "uv_set", 1 },
          { "uv_transform",
            { { "scale", { 2.0F, 3.0F } }, { "offset", { 0.1F, 0.2F } },
              { "rotation_radians", 0.5F } } },
        });
    }
    document.emplace("textures", std::move(textures));
    std::vector<ImportDiagnostic> diagnostics;
    const auto source = MaterialSource::FromDescriptor(
      document.dump(), "source.omat.json", "", diagnostics);
    if (!source.has_value()) {
      FAIL() << "Expected source to contain a value";
    }
    EXPECT_TRUE(diagnostics.empty());
    EXPECT_EQ(MaterialSource::TextureSlots().size(), 12U);
    for (const auto& slot : MaterialSource::TextureSlots()) {
      const auto& binding = source->textures.*slot.binding;
      EXPECT_TRUE(binding.assigned);
      EXPECT_EQ(binding.source_id,
        "/Content/Textures/" + std::string(slot.name) + ".otex");
      EXPECT_EQ(binding.uv_set, 1U);
      EXPECT_EQ(binding.uv_transform.scale[0], 2.0F);
      EXPECT_EQ(binding.uv_transform.offset[1], 0.2F);
      EXPECT_EQ(binding.uv_transform.rotation_radians, 0.5F);
    }
  }

  NOLINT_TEST(MaterialSourceTest, PreservesNameAndHashingRecipePrecedence)
  {
    std::vector<ImportDiagnostic> diagnostics;
    const auto named = MaterialSource::FromDescriptor(
      R"({"name":"Authored","content_hashing":false})", "Fallback.omat.json",
      "Override", diagnostics);
    if (!named.has_value()) {
      FAIL() << "Expected named to contain a value";
    }
    EXPECT_EQ(named->name, "Override");
    EXPECT_EQ(named->storage_name, "Override");
    if (!named->content_hashing.has_value()) {
      FAIL() << "Expected named->content_hashing to contain a value";
    }
    EXPECT_FALSE(named->content_hashing.value());
    const auto authored = MaterialSource::FromDescriptor(
      R"({"name":"Authored"})", "Fallback.omat.json", "", diagnostics);
    if (!authored.has_value()) {
      FAIL() << "Expected authored to contain a value";
    }
    EXPECT_EQ(authored->name, "Authored");
    const auto fallback = MaterialSource::FromDescriptor(
      "{}", "Fallback.omat.json", "", diagnostics);
    if (!fallback.has_value()) {
      FAIL() << "Expected fallback to contain a value";
    }
    EXPECT_EQ(fallback->name, "Fallback.omat");
    EXPECT_FALSE(fallback->content_hashing.has_value());
  }

  NOLINT_TEST(MaterialSourceTest, PreparationRejectsDuplicateShaderStages)
  {
    std::vector<ImportDiagnostic> diagnostics;
    const auto result = MaterialSource::FromDescriptor(R"({"shaders":[
      {"stage":"pixel","source_path":"declared.hlsl","entry_point":"PS"},
      {"stage":"pixel","source_path":"another.hlsl","entry_point":"PS"}]})",
      "material.json", "", diagnostics);
    EXPECT_FALSE(result);
    EXPECT_TRUE(HasCode(diagnostics, "material.shader_stage_duplicate"));
  }

  NOLINT_TEST(MaterialSourceTest, PreparationRejectsUnsupportedEmissionRange)
  {
    std::vector<ImportDiagnostic> diagnostics;
    const auto result = MaterialSource::FromDescriptor(
      R"({"parameters":{"emissive_color":[1,1,1],"emissive_intensity":70000}})",
      "material.json", "", diagnostics);
    EXPECT_FALSE(result);
    EXPECT_TRUE(
      HasCode(diagnostics, "material.descriptor.schema_validation_failed")
      || HasCode(diagnostics, "material.emissive_factor_range"));
  }

  NOLINT_TEST(MaterialSourceTest, ForcePackedRequiresTheSameTextureAndUv)
  {
    std::vector<ImportDiagnostic> diagnostics;
    const auto result = MaterialSource::FromDescriptor(
      R"({"orm_policy":"force_packed","textures":{
      "metallic":{"virtual_path":"/Content/orm.otex"},
      "roughness":{"virtual_path":"/Content/orm.otex","uv_set":1}}})",
      "material.json", "", diagnostics);
    EXPECT_FALSE(result);
    EXPECT_TRUE(HasCode(diagnostics, "material.orm_policy"));
  }

  NOLINT_TEST(
    MaterialSourceTest, ValidatesModelAdapterValuesWithoutDescriptorParsing)
  {
    MaterialSource source;
    source.name = "Imported";
    source.inputs.emissive_factor[0] = std::numeric_limits<float>::infinity();
    std::vector<ImportDiagnostic> diagnostics;
    EXPECT_FALSE(source.Validate("model.gltf", diagnostics));
    EXPECT_TRUE(HasCode(diagnostics, "material.emissive_factor_range"));
  }
}
}
