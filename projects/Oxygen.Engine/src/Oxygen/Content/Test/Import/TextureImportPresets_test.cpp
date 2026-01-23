//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/TextureImportPresets.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>

namespace {

using oxygen::ColorSpace;
using oxygen::Format;
using oxygen::TextureType;
using oxygen::content::import::ApplyPreset;
using oxygen::content::import::Bc7Quality;
using oxygen::content::import::GetPresetMetadata;
using oxygen::content::import::MakeDescFromPreset;
using oxygen::content::import::MipFilter;
using oxygen::content::import::MipPolicy;
using oxygen::content::import::TextureImportDesc;
using oxygen::content::import::TextureIntent;
using oxygen::content::import::TexturePreset;
using oxygen::content::import::to_string;

//===----------------------------------------------------------------------===//
// to_string Tests
//===----------------------------------------------------------------------===//

class TexturePresetToStringTest : public ::testing::Test { };

//! Test: to_string returns non-null for all presets.
NOLINT_TEST_F(TexturePresetToStringTest, ReturnsNonNullForAllPresets)
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

class TexturePresetMetadataTest : public ::testing::Test { };

//! Test: GetPresetMetadata returns valid metadata for all presets.
NOLINT_TEST_F(TexturePresetMetadataTest, ReturnsValidMetadataForAllPresets)
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

class TexturePresetAlbedoTest : public ::testing::Test { };

//! Test: Albedo preset sets correct values.
NOLINT_TEST_F(TexturePresetAlbedoTest, SetsCorrectValues)
{
  // Arrange
  TextureImportDesc desc;

  // Act
  ApplyPreset(desc, TexturePreset::kAlbedo);

  // Assert
  EXPECT_EQ(desc.intent, TextureIntent::kAlbedo);
  EXPECT_EQ(desc.source_color_space, ColorSpace::kSRGB);
  EXPECT_EQ(desc.mip_policy, MipPolicy::kFullChain);
  EXPECT_EQ(desc.mip_filter, MipFilter::kBox);
  EXPECT_EQ(desc.output_format, Format::kBC7UNormSRGB);
  EXPECT_EQ(desc.bc7_quality, Bc7Quality::kDefault);
}

class TexturePresetNormalTest : public ::testing::Test { };

//! Test: Normal preset sets correct values.
NOLINT_TEST_F(TexturePresetNormalTest, SetsCorrectValues)
{
  // Arrange
  TextureImportDesc desc;

  // Act
  ApplyPreset(desc, TexturePreset::kNormal);

  // Assert
  EXPECT_EQ(desc.intent, TextureIntent::kNormalTS);
  EXPECT_EQ(desc.source_color_space, ColorSpace::kLinear);
  EXPECT_TRUE(desc.renormalize_normals_in_mips);
  EXPECT_EQ(desc.output_format, Format::kBC7UNorm);
  EXPECT_EQ(desc.bc7_quality, Bc7Quality::kDefault);
}

class TexturePresetORMPackedTest : public ::testing::Test { };

//! Test: ORM packed preset sets correct values.
NOLINT_TEST_F(TexturePresetORMPackedTest, SetsCorrectValues)
{
  // Arrange
  TextureImportDesc desc;

  // Act
  ApplyPreset(desc, TexturePreset::kORMPacked);

  // Assert
  EXPECT_EQ(desc.intent, TextureIntent::kORMPacked);
  EXPECT_EQ(desc.source_color_space, ColorSpace::kLinear);
  EXPECT_EQ(desc.output_format, Format::kBC7UNorm);
  EXPECT_EQ(desc.bc7_quality, Bc7Quality::kDefault);
}

class TexturePresetUITest : public ::testing::Test { };

//! Test: UI preset uses Lanczos filter for sharpness.
NOLINT_TEST_F(TexturePresetUITest, UsesLanczosFilter)
{
  // Arrange
  TextureImportDesc desc;

  // Act
  ApplyPreset(desc, TexturePreset::kUI);

  // Assert
  EXPECT_EQ(desc.mip_filter, MipFilter::kLanczos);
  EXPECT_EQ(desc.source_color_space, ColorSpace::kSRGB);
  EXPECT_EQ(desc.output_format, Format::kBC7UNormSRGB);
}

//===----------------------------------------------------------------------===//
// ApplyPreset Tests - HDR Presets
//===----------------------------------------------------------------------===//

class TexturePresetHdrEnvironmentTest : public ::testing::Test { };

//! Test: HDR environment preset sets correct values.
NOLINT_TEST_F(TexturePresetHdrEnvironmentTest, SetsCorrectValues)
{
  // Arrange
  TextureImportDesc desc;

  // Act
  ApplyPreset(desc, TexturePreset::kHdrEnvironment);

  // Assert
  EXPECT_EQ(desc.intent, TextureIntent::kHdrEnvironment);
  EXPECT_EQ(desc.texture_type, TextureType::kTextureCube);
  EXPECT_EQ(desc.source_color_space, ColorSpace::kLinear);
  EXPECT_EQ(desc.output_format, Format::kRGBA16Float);
  EXPECT_EQ(desc.bc7_quality, Bc7Quality::kNone);
}

class TexturePresetHdrLightProbeTest : public ::testing::Test { };

//! Test: HDR light probe preset sets correct values.
NOLINT_TEST_F(TexturePresetHdrLightProbeTest, SetsCorrectValues)
{
  // Arrange
  TextureImportDesc desc;

  // Act
  ApplyPreset(desc, TexturePreset::kHdrLightProbe);

  // Assert
  EXPECT_EQ(desc.intent, TextureIntent::kHdrLightProbe);
  EXPECT_EQ(desc.source_color_space, ColorSpace::kLinear);
  EXPECT_EQ(desc.output_format, Format::kRGBA16Float);
  EXPECT_EQ(desc.bc7_quality, Bc7Quality::kNone);
}

//===----------------------------------------------------------------------===//
// MakeDescFromPreset Tests
//===----------------------------------------------------------------------===//

class MakeDescFromPresetTest : public ::testing::Test { };

//! Test: MakeDescFromPreset creates descriptor with preset applied.
NOLINT_TEST_F(MakeDescFromPresetTest, CreatesDescriptorWithPreset)
{
  // Act
  auto desc = MakeDescFromPreset(TexturePreset::kAlbedo);

  // Assert
  EXPECT_EQ(desc.intent, TextureIntent::kAlbedo);
  EXPECT_EQ(desc.source_color_space, ColorSpace::kSRGB);
  EXPECT_EQ(desc.output_format, Format::kBC7UNormSRGB);
}

//! Test: MakeDescFromPreset preserves unset identity fields.
NOLINT_TEST_F(MakeDescFromPresetTest, LeavesIdentityFieldsUnset)
{
  // Act
  auto desc = MakeDescFromPreset(TexturePreset::kNormal);

  // Assert - identity fields should be defaults
  EXPECT_TRUE(desc.source_id.empty());
  EXPECT_EQ(desc.width, 0u);
  EXPECT_EQ(desc.height, 0u);
}

} // namespace
