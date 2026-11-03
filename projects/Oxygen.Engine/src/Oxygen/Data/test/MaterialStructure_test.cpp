//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Data/MaterialDescriptor.h, Data/ShaderReference.h

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/testing/DescriptorDefects.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/ProductRefusal.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::ShaderType;
using oxygen::cooker::testing::DescriptorDefects;
using oxygen::cooker::testing::MaterialBuilder;
using oxygen::cooker::testing::MaterialRefusal;
using oxygen::data::CookedDefect;
using oxygen::data::MaterialDomain;
using oxygen::data::pak::render::MaterialAssetDesc;

//! The stage bit of `type` in a material's shader stages.
constexpr auto StageBit(const ShaderType type) -> uint32_t
{
  return 1U << static_cast<uint32_t>(type);
}

//! A material declares values the format defines, and one shader reference
//! per declared stage, each naming a canonical source, an entry point and
//! canonical defines.
class MaterialStructureTest : public testing::Test {
protected:
  //! An opaque material with one pixel shader, `PS` of `fixture.hlsl`
  //! compiled with `ALPHA,LOD=2`.
  [[nodiscard]] static auto Material() -> std::vector<std::byte>
  {
    return MaterialBuilder { "Wood" }
      .PixelShader("fixture.hlsl", "PS", "ALPHA,LOD=2")
      .Bytes();
  }

  //! `Material()` with its descriptor record changed by `edit`.
  template <typename Edit>
  [[nodiscard]] static auto WithRecord(const Edit& edit)
    -> std::vector<std::byte>
  {
    auto material = Material();
    auto record = DescriptorDefects::MaterialAt(material);
    edit(record);
    DescriptorDefects::SetMaterial(material, record);
    return material;
  }

  //! `Material()` with the string `from` replaced by `to`, of its length.
  [[nodiscard]] static auto WithString(const std::string_view from,
    const std::string_view to) -> std::vector<std::byte>
  {
    auto material = Material();
    DescriptorDefects::ReplaceString(material, from, to);
    return material;
  }
};

NOLINT_TEST_F(MaterialStructureTest, ReadsAWellFormedMaterial)
{
  EXPECT_EQ(MaterialRefusal(Material()), std::nullopt);
}

//! The domain is one the format defines.
NOLINT_TEST_F(MaterialStructureTest, RefusesAnUnknownDomain)
{
  for (const auto domain : {
         MaterialDomain::kUnknown,
         static_cast<MaterialDomain>(
           static_cast<uint8_t>(MaterialDomain::kMaxMaterialDomain) + 1U),
       }) {
    SCOPED_TRACE(static_cast<int>(domain));
    EXPECT_EQ(
      MaterialRefusal(WithRecord([domain](MaterialAssetDesc& record) -> void {
        record.material_domain = static_cast<uint8_t>(domain);
      })),
      CookedDefect::kInvalidValue);
  }
}

//! Flags set only the bits the format defines.
NOLINT_TEST_F(MaterialStructureTest, RefusesUnknownFlags)
{
  EXPECT_EQ(MaterialRefusal(WithRecord([](MaterialAssetDesc& record) -> void {
    record.flags |= 1U << 31U;
  })),
    CookedDefect::kInvalidValue);
}

//! A stage is a shader type, even when its reference names the same value.
NOLINT_TEST_F(MaterialStructureTest, RefusesAnUnknownStage)
{
  constexpr auto kUnknown = uint8_t { 20 };
  auto material = WithRecord([](MaterialAssetDesc& record) -> void {
    record.shader_stages = 1U << kUnknown;
  });
  auto shader = DescriptorDefects::ShaderReferenceAt(material, 0);
  shader.shader_type = kUnknown;
  DescriptorDefects::SetShaderReference(material, 0, shader);

  EXPECT_EQ(MaterialRefusal(material), CookedDefect::kInvalidValue);
}

//! The shader references are the declared stages.
NOLINT_TEST_F(MaterialStructureTest, RefusesShadersOfUndeclaredStages)
{
  EXPECT_EQ(MaterialRefusal(WithRecord([](MaterialAssetDesc& record) -> void {
    record.shader_stages = StageBit(ShaderType::kVertex);
  })),
    CookedDefect::kInconsistent);
}

//! An entry point is an identifier.
NOLINT_TEST_F(MaterialStructureTest, RefusesAMalformedEntryPoint)
{
  EXPECT_EQ(
    MaterialRefusal(WithString("PS", "2S")), CookedDefect::kInvalidValue);
  EXPECT_EQ(
    MaterialRefusal(WithString("PS", "P-")), CookedDefect::kInvalidValue);
}

//! A source path is canonical and within the shaders directory.
NOLINT_TEST_F(MaterialStructureTest, RefusesANonCanonicalSourcePath)
{
  EXPECT_EQ(MaterialRefusal(WithString("fixture.hlsl", "../fixture.h")),
    CookedDefect::kInvalidPath);
  EXPECT_EQ(MaterialRefusal(WithString("fixture.hlsl", "fix\\ure.hlsl")),
    CookedDefect::kInvalidPath);
  EXPECT_EQ(MaterialRefusal(WithString("fixture.hlsl", "C:fixture.hl")),
    CookedDefect::kInvalidPath);
}

//! Defines are well-formed entries sorted by name.
NOLINT_TEST_F(MaterialStructureTest, RefusesMalformedDefines)
{
  EXPECT_EQ(MaterialRefusal(WithString("ALPHA,LOD=2", "alpha,LOD=2")),
    CookedDefect::kInvalidValue);
  EXPECT_EQ(MaterialRefusal(WithString("ALPHA,LOD=2", "ALPHA,LOD= ")),
    CookedDefect::kInvalidValue);
  EXPECT_EQ(MaterialRefusal(WithString("ALPHA,LOD=2", "LOD=2,ALPHA")),
    CookedDefect::kNotCanonical);
}

//! Emissive factors are finite and within the format's range.
NOLINT_TEST_F(MaterialStructureTest, RefusesAnEmissiveFactorOutOfRange)
{
  EXPECT_EQ(MaterialRefusal(WithRecord([](MaterialAssetDesc& record) -> void {
    record.emissive_factor[1] = std::numeric_limits<float>::quiet_NaN();
  })),
    CookedDefect::kInvalidValue);
  EXPECT_EQ(MaterialRefusal(WithRecord([](MaterialAssetDesc& record) -> void {
    record.emissive_factor[2] = -1.0F;
  })),
    CookedDefect::kInvalidValue);
}

} // namespace
