//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/TextureImportPresets.cpp, Import/TextureImporter.cpp

#include <array>
#include <filesystem>

#include <Oxygen/Cooker/Import/TextureImportDesc.h>
#include <Oxygen/Cooker/Import/TextureImportPresets.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Cooker/Import/TextureImporter.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::ColorSpace;
using oxygen::Format;
using oxygen::TextureType;
using oxygen::content::import::ApplyPreset;
using oxygen::content::import::Bc7Quality;
using oxygen::content::import::DetectPresetFromFilename;
using oxygen::content::import::GetPresetMetadata;
using oxygen::content::import::MakeDescFromPreset;
using oxygen::content::import::MipFilter;
using oxygen::content::import::MipPolicy;
using oxygen::content::import::TextureImportDesc;
using oxygen::content::import::TextureIntent;
using oxygen::content::import::TexturePreset;
using oxygen::content::import::to_string;

//! Preset detection considers the filename, handles case and preserves
//! filesystem stem/extension rules without converting native path characters.
NOLINT_TEST(TextureImportPresetsTest, DetectPresetFromFilename)
{
  struct Case {
    std::filesystem::path path;
    TexturePreset expected;
  };
  const auto cases = std::array {
    Case {
      .path = "textures/stone_NORMAL.png",
      .expected = TexturePreset::kNormal,
    },
    Case {
      .path = "textures/stone_albedo.EXR",
      .expected = TexturePreset::kHdrEnvironment,
    },
    Case {
      .path = "textures/stone_roughness",
      .expected = TexturePreset::kRoughness,
    },
    Case {
      .path = "textures/.hidden_albedo",
      .expected = TexturePreset::kAlbedo,
    },
    Case {
      .path = "textures_albedo/other.png",
      .expected = TexturePreset::kData,
    },
    Case { .path = "textures/", .expected = TexturePreset::kData },
    Case { .path = ".", .expected = TexturePreset::kData },
    Case { .path = "..", .expected = TexturePreset::kData },
#ifdef _WIN32
    Case { .path = L"C:stone_NRM.png", .expected = TexturePreset::kNormal },
    Case {
      .path = L"textures/\u96ea_NORMAL.png",
      .expected = TexturePreset::kNormal,
    },
    Case { .path = L"\\\\server_albedo", .expected = TexturePreset::kData },
#endif
  };
  for (const auto& test_case : cases) {
    EXPECT_EQ(DetectPresetFromFilename(test_case.path), test_case.expected)
      << test_case.path;
  }
}

//===----------------------------------------------------------------------===//
// to_string Tests
//===----------------------------------------------------------------------===//

NOLINT_TEST(TextureImportPresetsTest, ToStringReturnsPresetNames)
{
  EXPECT_STREQ(to_string(TexturePreset::kAlbedo), "Albedo");
  EXPECT_STREQ(to_string(TexturePreset::kNormal), "Normal");
  EXPECT_STREQ(to_string(TexturePreset::kRoughness), "Roughness");
  EXPECT_STREQ(to_string(TexturePreset::kMetallic), "Metallic");
  EXPECT_STREQ(to_string(TexturePreset::kAO), "AO");
  EXPECT_STREQ(to_string(TexturePreset::kORMPacked), "ORMPacked");
  EXPECT_STREQ(to_string(TexturePreset::kEmissive), "Emissive");
  EXPECT_STREQ(to_string(TexturePreset::kUI), "UI");
  EXPECT_STREQ(to_string(TexturePreset::kHdrEnvironment), "HdrEnvironment");
  EXPECT_STREQ(to_string(TexturePreset::kHdrLightProbe), "HdrLightProbe");
  EXPECT_STREQ(to_string(TexturePreset::kData), "Data");
}

//===----------------------------------------------------------------------===//
// GetPresetMetadata Tests
//===----------------------------------------------------------------------===//

NOLINT_TEST(TextureImportPresetsTest, GetPresetMetadataReturnsValidMetadata)
{
  const auto albedo = GetPresetMetadata(TexturePreset::kAlbedo);
  EXPECT_NE(albedo.name, nullptr);
  EXPECT_NE(albedo.description, nullptr);
  EXPECT_FALSE(albedo.is_hdr);
  EXPECT_TRUE(albedo.uses_bc7);

  const auto hdr_env = GetPresetMetadata(TexturePreset::kHdrEnvironment);
  EXPECT_NE(hdr_env.name, nullptr);
  EXPECT_TRUE(hdr_env.is_hdr);
  EXPECT_FALSE(hdr_env.uses_bc7);
}

//===----------------------------------------------------------------------===//
// ApplyPreset Tests - LDR Material Presets
//===----------------------------------------------------------------------===//

NOLINT_TEST(TextureImportPresetsTest, ApplyAlbedoPresetSetsExpectedDescriptor)
{
  TextureImportDesc desc;

  ApplyPreset(desc, TexturePreset::kAlbedo);

  EXPECT_EQ(desc.intent, TextureIntent::kAlbedo);
  EXPECT_EQ(desc.source_color_space, ColorSpace::kSRGB);
  EXPECT_EQ(desc.mip_policy, MipPolicy::kFullChain);
  EXPECT_EQ(desc.mip_filter, MipFilter::kBox);
  EXPECT_EQ(desc.output_format, Format::kBC7UNormSRGB);
  EXPECT_EQ(desc.bc7_quality, Bc7Quality::kDefault);
}

NOLINT_TEST(TextureImportPresetsTest, ApplyNormalPresetSetsExpectedDescriptor)
{
  TextureImportDesc desc;

  ApplyPreset(desc, TexturePreset::kNormal);

  EXPECT_EQ(desc.intent, TextureIntent::kNormalTS);
  EXPECT_EQ(desc.source_color_space, ColorSpace::kLinear);
  EXPECT_TRUE(desc.renormalize_normals_in_mips);
  EXPECT_EQ(desc.output_format, Format::kBC7UNorm);
  EXPECT_EQ(desc.bc7_quality, Bc7Quality::kDefault);
}

NOLINT_TEST(
  TextureImportPresetsTest, ApplyOrmPackedPresetSetsExpectedDescriptor)
{
  TextureImportDesc desc;

  ApplyPreset(desc, TexturePreset::kORMPacked);

  EXPECT_EQ(desc.intent, TextureIntent::kORMPacked);
  EXPECT_EQ(desc.source_color_space, ColorSpace::kLinear);
  EXPECT_EQ(desc.output_format, Format::kBC7UNorm);
  EXPECT_EQ(desc.bc7_quality, Bc7Quality::kDefault);
}

NOLINT_TEST(TextureImportPresetsTest, ApplyUiPresetUsesLanczosFilter)
{
  TextureImportDesc desc;

  ApplyPreset(desc, TexturePreset::kUI);

  EXPECT_EQ(desc.mip_filter, MipFilter::kLanczos);
  EXPECT_EQ(desc.source_color_space, ColorSpace::kSRGB);
  EXPECT_EQ(desc.output_format, Format::kBC7UNormSRGB);
}

//===----------------------------------------------------------------------===//
// ApplyPreset Tests - HDR Presets
//===----------------------------------------------------------------------===//

NOLINT_TEST(
  TextureImportPresetsTest, ApplyHdrEnvironmentPresetSetsExpectedDescriptor)
{
  TextureImportDesc desc;

  ApplyPreset(desc, TexturePreset::kHdrEnvironment);

  EXPECT_EQ(desc.intent, TextureIntent::kHdrEnvironment);
  EXPECT_EQ(desc.texture_type, TextureType::kTextureCube);
  EXPECT_EQ(desc.source_color_space, ColorSpace::kLinear);
  EXPECT_EQ(desc.output_format, Format::kRGBA16Float);
  EXPECT_EQ(desc.bc7_quality, Bc7Quality::kNone);
}

NOLINT_TEST(
  TextureImportPresetsTest, ApplyHdrLightProbePresetSetsExpectedDescriptor)
{
  TextureImportDesc desc;

  ApplyPreset(desc, TexturePreset::kHdrLightProbe);

  EXPECT_EQ(desc.intent, TextureIntent::kHdrLightProbe);
  EXPECT_EQ(desc.source_color_space, ColorSpace::kLinear);
  EXPECT_EQ(desc.output_format, Format::kRGBA16Float);
  EXPECT_EQ(desc.bc7_quality, Bc7Quality::kNone);
}

//===----------------------------------------------------------------------===//
// MakeDescFromPreset Tests
//===----------------------------------------------------------------------===//

NOLINT_TEST(TextureImportPresetsTest, MakeDescFromPresetAppliesPreset)
{
  auto desc = MakeDescFromPreset(TexturePreset::kAlbedo);

  EXPECT_EQ(desc.intent, TextureIntent::kAlbedo);
  EXPECT_EQ(desc.source_color_space, ColorSpace::kSRGB);
  EXPECT_EQ(desc.output_format, Format::kBC7UNormSRGB);
}

NOLINT_TEST(
  TextureImportPresetsTest, MakeDescFromPresetLeavesIdentityFieldsUnset)
{
  auto desc = MakeDescFromPreset(TexturePreset::kNormal);

  // Identity fields should be defaults
  EXPECT_TRUE(desc.source_id.empty());
  EXPECT_EQ(desc.width, 0U);
  EXPECT_EQ(desc.height, 0U);
}

} // namespace
