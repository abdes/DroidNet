//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/TextureImportPresets.h>

namespace oxygen::content::import {

auto to_string(const TexturePreset value) -> const char*
{
  switch (value) {
  case TexturePreset::kAlbedo:
    return "Albedo";
  case TexturePreset::kNormal:
    return "Normal";
  case TexturePreset::kRoughness:
    return "Roughness";
  case TexturePreset::kMetallic:
    return "Metallic";
  case TexturePreset::kAO:
    return "AO";
  case TexturePreset::kORMPacked:
    return "ORMPacked";
  case TexturePreset::kEmissive:
    return "Emissive";
  case TexturePreset::kUI:
    return "UI";
  case TexturePreset::kHdrEnvironment:
    return "HdrEnvironment";
  case TexturePreset::kHdrLightProbe:
    return "HdrLightProbe";
  case TexturePreset::kData:
    return "Data";
  case TexturePreset::kHeightMap:
    return "HeightMap";
  }
  return "Unknown";
}

auto GetPresetMetadata(const TexturePreset preset) noexcept
  -> TexturePresetMetadata
{
  switch (preset) {
  case TexturePreset::kAlbedo:
    return {
      .name = "Albedo / Base Color",
      .description = "Diffuse albedo texture with sRGB color space",
      .is_hdr = false,
      .uses_bc7 = true,
    };

  case TexturePreset::kNormal:
    return {
      .name = "Normal Map (Tangent-Space)",
      .description = "Tangent-space normal map with renormalization",
      .is_hdr = false,
      .uses_bc7 = true,
    };

  case TexturePreset::kRoughness:
    return {
      .name = "Roughness",
      .description = "Roughness map (single channel, linear)",
      .is_hdr = false,
      .uses_bc7 = true,
    };

  case TexturePreset::kMetallic:
    return {
      .name = "Metallic",
      .description = "Metallic map (single channel, linear)",
      .is_hdr = false,
      .uses_bc7 = true,
    };

  case TexturePreset::kAO:
    return {
      .name = "Ambient Occlusion",
      .description = "Ambient occlusion map (single channel, linear)",
      .is_hdr = false,
      .uses_bc7 = true,
    };

  case TexturePreset::kORMPacked:
    return {
      .name = "ORM Packed",
      .description = "Packed ORM: R=AO, G=Roughness, B=Metallic",
      .is_hdr = false,
      .uses_bc7 = true,
    };

  case TexturePreset::kEmissive:
    return {
      .name = "Emissive",
      .description = "Emissive color with sRGB color space",
      .is_hdr = false,
      .uses_bc7 = true,
    };

  case TexturePreset::kUI:
    return {
      .name = "UI / Text",
      .description = "UI elements with Lanczos filter for sharp detail",
      .is_hdr = false,
      .uses_bc7 = true,
    };

  case TexturePreset::kHdrEnvironment:
    return {
      .name = "HDR Environment",
      .description = "HDR skybox in RGBA16Float",
      .is_hdr = true,
      .uses_bc7 = false,
    };

  case TexturePreset::kHdrLightProbe:
    return {
      .name = "HDR Light Probe",
      .description = "HDR light probe for IBL in RGBA16Float",
      .is_hdr = true,
      .uses_bc7 = false,
    };

  case TexturePreset::kData:
    return {
      .name = "Data",
      .description = "Generic data texture (linear, no special handling)",
      .is_hdr = false,
      .uses_bc7 = false,
    };

  case TexturePreset::kHeightMap:
    return {
      .name = "Height / Displacement Map",
      .description
      = "Height map for displacement or parallax mapping (R16UNorm)",
      .is_hdr = false,
      .uses_bc7 = false,
    };
  }

  return {
    .name = "Unknown",
    .description = "Unknown preset",
    .is_hdr = false,
    .uses_bc7 = false,
  };
}

void ApplyPreset(TextureImportDesc& desc, const TexturePreset preset) noexcept
{
  // Reset to defaults before applying preset
  desc.intent = TextureIntent::kData;
  desc.flip_y_on_decode = false;
  desc.force_rgba_on_decode = true;
  desc.source_color_space = ColorSpace::kLinear;
  desc.flip_normal_green = false;
  desc.renormalize_normals_in_mips = false;
  desc.mip_policy = MipPolicy::kFullChain;
  desc.max_mip_levels = 1;
  desc.mip_filter = MipFilter::kBox;
  desc.mip_filter_space = ColorSpace::kLinear;
  desc.output_format = Format::kRGBA8UNorm;
  desc.bc7_quality = Bc7Quality::kNone;
  desc.bake_hdr_to_ldr = false;
  desc.exposure_ev = 0.0F;

  switch (preset) {
  case TexturePreset::kAlbedo:
    desc.intent = TextureIntent::kAlbedo;
    desc.source_color_space = ColorSpace::kSRGB;
    desc.mip_filter_space = ColorSpace::kSRGB;
    desc.output_format = Format::kBC7UNormSRGB;
    desc.bc7_quality = Bc7Quality::kDefault;
    break;

  case TexturePreset::kNormal:
    desc.intent = TextureIntent::kNormalTS;
    desc.source_color_space = ColorSpace::kLinear;
    desc.renormalize_normals_in_mips = true;
    desc.output_format = Format::kBC7UNorm;
    desc.bc7_quality = Bc7Quality::kDefault;
    break;

  case TexturePreset::kRoughness:
    desc.intent = TextureIntent::kRoughness;
    desc.source_color_space = ColorSpace::kLinear;
    desc.output_format = Format::kBC7UNorm;
    desc.bc7_quality = Bc7Quality::kDefault;
    break;

  case TexturePreset::kMetallic:
    desc.intent = TextureIntent::kMetallic;
    desc.source_color_space = ColorSpace::kLinear;
    desc.output_format = Format::kBC7UNorm;
    desc.bc7_quality = Bc7Quality::kDefault;
    break;

  case TexturePreset::kAO:
    desc.intent = TextureIntent::kAO;
    desc.source_color_space = ColorSpace::kLinear;
    desc.output_format = Format::kBC7UNorm;
    desc.bc7_quality = Bc7Quality::kDefault;
    break;

  case TexturePreset::kORMPacked:
    desc.intent = TextureIntent::kORMPacked;
    desc.source_color_space = ColorSpace::kLinear;
    desc.output_format = Format::kBC7UNorm;
    desc.bc7_quality = Bc7Quality::kDefault;
    break;

  case TexturePreset::kEmissive:
    desc.intent = TextureIntent::kEmissive;
    desc.source_color_space = ColorSpace::kSRGB;
    desc.mip_filter_space = ColorSpace::kSRGB;
    desc.output_format = Format::kBC7UNormSRGB;
    desc.bc7_quality = Bc7Quality::kDefault;
    break;

  case TexturePreset::kUI:
    desc.intent = TextureIntent::kData;
    desc.source_color_space = ColorSpace::kSRGB;
    desc.mip_filter = MipFilter::kLanczos;
    desc.mip_filter_space = ColorSpace::kSRGB;
    desc.output_format = Format::kBC7UNormSRGB;
    desc.bc7_quality = Bc7Quality::kDefault;
    break;

  case TexturePreset::kHdrEnvironment:
    desc.intent = TextureIntent::kHdrEnvironment;
    desc.texture_type = TextureType::kTextureCube;
    desc.source_color_space = ColorSpace::kLinear;
    desc.output_format = Format::kRGBA16Float;
    desc.bc7_quality = Bc7Quality::kNone;
    break;

  case TexturePreset::kHdrLightProbe:
    desc.intent = TextureIntent::kHdrLightProbe;
    desc.source_color_space = ColorSpace::kLinear;
    desc.output_format = Format::kRGBA16Float;
    desc.bc7_quality = Bc7Quality::kNone;
    break;

  case TexturePreset::kData:
    desc.intent = TextureIntent::kData;
    desc.source_color_space = ColorSpace::kLinear;
    desc.output_format = Format::kRGBA8UNorm;
    desc.bc7_quality = Bc7Quality::kNone;
    break;

  case TexturePreset::kHeightMap:
    desc.intent = TextureIntent::kHeightMap;
    desc.source_color_space = ColorSpace::kLinear;
    desc.output_format = Format::kR16UNorm;
    desc.bc7_quality = Bc7Quality::kNone;
    break;
  }
}

auto MakeDescFromPreset(const TexturePreset preset) noexcept
  -> TextureImportDesc
{
  TextureImportDesc desc;
  ApplyPreset(desc, preset);
  return desc;
}

} // namespace oxygen::content::import
