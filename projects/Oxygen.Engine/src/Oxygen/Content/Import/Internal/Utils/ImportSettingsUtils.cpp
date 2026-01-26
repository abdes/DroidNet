//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/Internal/Utils/ImportSettingsUtils.h>
#include <Oxygen/Content/Import/TextureImportPresets.h>
#include <Oxygen/Core/Types/Format.h>

namespace oxygen::content::import::internal {

namespace {

  struct PresetSpec final {
    TexturePreset preset;
    std::optional<ColorSpace> source_color_space;
    std::optional<Format> output_format;
    std::optional<Format> data_format;
  };

  auto ResolvePresetSpec(std::string_view value) -> std::optional<PresetSpec>
  {
    if (value == "albedo" || value == "albedo-srgb") {
      return PresetSpec { .preset = TexturePreset::kAlbedo };
    }
    if (value == "albedo-linear") {
      return PresetSpec {
        .preset = TexturePreset::kAlbedo,
        .source_color_space = ColorSpace::kLinear,
        .output_format = Format::kBC7UNorm,
        .data_format = Format::kBC7UNorm,
      };
    }
    if (value == "normal" || value == "normal-bc7") {
      return PresetSpec { .preset = TexturePreset::kNormal };
    }
    if (value == "roughness") {
      return PresetSpec { .preset = TexturePreset::kRoughness };
    }
    if (value == "metallic") {
      return PresetSpec { .preset = TexturePreset::kMetallic };
    }
    if (value == "ao") {
      return PresetSpec { .preset = TexturePreset::kAO };
    }
    if (value == "orm" || value == "orm-bc7") {
      return PresetSpec { .preset = TexturePreset::kORMPacked };
    }
    if (value == "emissive") {
      return PresetSpec { .preset = TexturePreset::kEmissive };
    }
    if (value == "ui") {
      return PresetSpec { .preset = TexturePreset::kUI };
    }
    if (value == "hdr-env") {
      return PresetSpec { .preset = TexturePreset::kHdrEnvironment };
    }
    if (value == "hdr-env-16f") {
      return PresetSpec {
        .preset = TexturePreset::kHdrEnvironment,
        .output_format = Format::kRGBA16Float,
        .data_format = Format::kRGBA16Float,
      };
    }
    if (value == "hdr-env-32f") {
      return PresetSpec {
        .preset = TexturePreset::kHdrEnvironment,
        .output_format = Format::kRGBA32Float,
        .data_format = Format::kRGBA32Float,
      };
    }
    if (value == "hdr-probe") {
      return PresetSpec { .preset = TexturePreset::kHdrLightProbe };
    }
    if (value == "data") {
      return PresetSpec { .preset = TexturePreset::kData };
    }
    if (value == "height") {
      return PresetSpec { .preset = TexturePreset::kHeightMap };
    }
    return std::nullopt;
  }

  [[nodiscard]] constexpr auto IsBc7Format(const Format format) noexcept -> bool
  {
    return format == Format::kBC7UNorm || format == Format::kBC7UNormSRGB;
  }

} // namespace

auto ParseIntent(std::string_view value) -> std::optional<TextureIntent>
{
  if (value == "albedo") {
    return TextureIntent::kAlbedo;
  }
  if (value == "normal") {
    return TextureIntent::kNormalTS;
  }
  if (value == "roughness") {
    return TextureIntent::kRoughness;
  }
  if (value == "metallic") {
    return TextureIntent::kMetallic;
  }
  if (value == "ao") {
    return TextureIntent::kAO;
  }
  if (value == "emissive") {
    return TextureIntent::kEmissive;
  }
  if (value == "opacity") {
    return TextureIntent::kOpacity;
  }
  if (value == "orm") {
    return TextureIntent::kORMPacked;
  }
  if (value == "hdr_env" || value == "hdr-env") {
    return TextureIntent::kHdrEnvironment;
  }
  if (value == "hdr_probe" || value == "hdr-probe") {
    return TextureIntent::kHdrLightProbe;
  }
  if (value == "data") {
    return TextureIntent::kData;
  }
  if (value == "height") {
    return TextureIntent::kHeightMap;
  }
  return std::nullopt;
}

auto ParseColorSpace(std::string_view value) -> std::optional<ColorSpace>
{
  if (value == "srgb") {
    return ColorSpace::kSRGB;
  }
  if (value == "linear") {
    return ColorSpace::kLinear;
  }
  return std::nullopt;
}

auto ParseFormat(std::string_view value) -> std::optional<Format>
{
  if (value == "rgba8") {
    return Format::kRGBA8UNorm;
  }
  if (value == "rgba8_srgb" || value == "rgba8-srgb") {
    return Format::kRGBA8UNormSRGB;
  }
  if (value == "bc7") {
    return Format::kBC7UNorm;
  }
  if (value == "bc7_srgb" || value == "bc7-srgb") {
    return Format::kBC7UNormSRGB;
  }
  if (value == "rgba16f") {
    return Format::kRGBA16Float;
  }
  if (value == "rgba32f") {
    return Format::kRGBA32Float;
  }
  return std::nullopt;
}

auto ParseMipPolicy(std::string_view value) -> std::optional<MipPolicy>
{
  if (value == "none") {
    return MipPolicy::kNone;
  }
  if (value == "full") {
    return MipPolicy::kFullChain;
  }
  if (value == "max") {
    return MipPolicy::kMaxCount;
  }
  return std::nullopt;
}

auto ParseMipFilter(std::string_view value) -> std::optional<MipFilter>
{
  if (value == "box") {
    return MipFilter::kBox;
  }
  if (value == "kaiser") {
    return MipFilter::kKaiser;
  }
  if (value == "lanczos") {
    return MipFilter::kLanczos;
  }
  return std::nullopt;
}

auto ParseBc7Quality(std::string_view value) -> std::optional<Bc7Quality>
{
  if (value == "none") {
    return Bc7Quality::kNone;
  }
  if (value == "fast") {
    return Bc7Quality::kFast;
  }
  if (value == "default") {
    return Bc7Quality::kDefault;
  }
  if (value == "high") {
    return Bc7Quality::kHigh;
  }
  return std::nullopt;
}

auto ParsePreset(std::string_view value) -> std::optional<TexturePreset>
{
  const auto spec = ResolvePresetSpec(value);
  if (!spec.has_value()) {
    return std::nullopt;
  }
  return spec->preset;
}

auto ParseHdrHandling(std::string_view value) -> std::optional<HdrHandling>
{
  if (value == "error") {
    return HdrHandling::kError;
  }
  if (value == "tonemap" || value == "auto") {
    return HdrHandling::kTonemapAuto;
  }
  if (value == "keep" || value == "float") {
    return HdrHandling::kKeepFloat;
  }
  return std::nullopt;
}

auto ParseCubeLayout(std::string_view value)
  -> std::optional<CubeMapImageLayout>
{
  if (value == "auto") {
    return CubeMapImageLayout::kAuto;
  }
  if (value == "hstrip") {
    return CubeMapImageLayout::kHorizontalStrip;
  }
  if (value == "vstrip") {
    return CubeMapImageLayout::kVerticalStrip;
  }
  if (value == "hcross") {
    return CubeMapImageLayout::kHorizontalCross;
  }
  if (value == "vcross") {
    return CubeMapImageLayout::kVerticalCross;
  }
  return std::nullopt;
}

auto DefaultColorSpaceForIntent(const TextureIntent intent) -> ColorSpace
{
  switch (intent) {
  case TextureIntent::kAlbedo:
  case TextureIntent::kEmissive:
    return ColorSpace::kSRGB;
  default:
    return ColorSpace::kLinear;
  }
}

auto DefaultFormatForIntent(const TextureIntent intent) -> Format
{
  switch (intent) {
  case TextureIntent::kAlbedo:
  case TextureIntent::kEmissive:
    return Format::kBC7UNormSRGB;
  case TextureIntent::kNormalTS:
  case TextureIntent::kORMPacked:
  case TextureIntent::kRoughness:
  case TextureIntent::kMetallic:
  case TextureIntent::kAO:
  case TextureIntent::kOpacity:
    return Format::kBC7UNorm;
  case TextureIntent::kHdrEnvironment:
  case TextureIntent::kHdrLightProbe:
    return Format::kRGBA16Float;
  case TextureIntent::kHeightMap:
    return Format::kR16UNorm;
  case TextureIntent::kData:
    return Format::kRGBA8UNorm;
  }
  return Format::kRGBA8UNorm;
}

auto MapSettingsToTuning(const TextureImportSettings& settings,
  ImportOptions::TextureTuning& tuning, std::ostream& error_stream) -> bool
{
  bool preset_applied = false;
  if (!settings.preset.empty()) {
    const auto spec = ResolvePresetSpec(settings.preset);
    if (!spec.has_value()) {
      error_stream << "ERROR: unknown preset: " << settings.preset << "\n";
      return false;
    }

    TextureImportDesc desc;
    ApplyPreset(desc, spec->preset);

    tuning.intent = desc.intent;
    tuning.source_color_space = desc.source_color_space;
    tuning.flip_y_on_decode = desc.flip_y_on_decode;
    tuning.force_rgba_on_decode = desc.force_rgba_on_decode;
    tuning.mip_policy = desc.mip_policy;
    tuning.max_mip_levels = desc.max_mip_levels;
    tuning.mip_filter = desc.mip_filter;
    tuning.mip_filter_space = desc.mip_filter_space;
    tuning.color_output_format = desc.output_format;
    tuning.data_output_format = desc.output_format;
    tuning.bc7_quality = desc.bc7_quality;
    tuning.hdr_handling = desc.hdr_handling;
    tuning.exposure_ev = desc.exposure_ev;
    tuning.bake_hdr_to_ldr = desc.bake_hdr_to_ldr;
    tuning.flip_normal_green = desc.flip_normal_green;
    tuning.renormalize_normals_in_mips = desc.renormalize_normals_in_mips;

    if (desc.texture_type == TextureType::kTextureCube) {
      tuning.import_cubemap = true;
    }

    if (spec->source_color_space.has_value()) {
      tuning.source_color_space = *spec->source_color_space;
    }
    if (spec->output_format.has_value()) {
      tuning.color_output_format = *spec->output_format;
    }
    if (spec->data_format.has_value()) {
      tuning.data_output_format = *spec->data_format;
    }

    preset_applied = true;
    tuning.enabled = true;
  }

  if (!settings.intent.empty()) {
    auto parsed = ParseIntent(settings.intent);
    if (!parsed.has_value()) {
      error_stream << "ERROR: invalid intent: " << settings.intent << "\n";
      return false;
    }
    tuning.intent = *parsed;
    tuning.enabled = true;
  } else if (!preset_applied) {
    tuning.enabled = true;
  }

  if (!settings.color_space.empty()) {
    auto parsed = ParseColorSpace(settings.color_space);
    if (!parsed.has_value()) {
      error_stream << "ERROR: invalid color_space: " << settings.color_space
                   << "\n";
      return false;
    }
    tuning.source_color_space = *parsed;
  } else if (!preset_applied && !settings.intent.empty()) {
    tuning.source_color_space = DefaultColorSpaceForIntent(tuning.intent);
  }

  if (!settings.output_format.empty()) {
    auto parsed = ParseFormat(settings.output_format);
    if (!parsed.has_value()) {
      error_stream << "ERROR: invalid output_format: " << settings.output_format
                   << "\n";
      return false;
    }
    tuning.color_output_format = *parsed;
    if (settings.data_format.empty()) {
      tuning.data_output_format = *parsed;
    }
  } else if (!preset_applied && !settings.intent.empty()) {
    const auto format = DefaultFormatForIntent(tuning.intent);
    tuning.color_output_format = format;
    tuning.data_output_format = format;
  }

  if (!settings.data_format.empty()) {
    auto parsed = ParseFormat(settings.data_format);
    if (!parsed.has_value()) {
      error_stream << "ERROR: invalid data_format: " << settings.data_format
                   << "\n";
      return false;
    }
    tuning.data_output_format = *parsed;
  }

  if (!settings.mip_policy.empty()) {
    auto parsed = ParseMipPolicy(settings.mip_policy);
    if (!parsed.has_value()) {
      error_stream << "ERROR: invalid mip_policy: " << settings.mip_policy
                   << "\n";
      return false;
    }
    tuning.mip_policy = *parsed;
  }

  if (!settings.mip_filter.empty()) {
    auto parsed = ParseMipFilter(settings.mip_filter);
    if (!parsed.has_value()) {
      error_stream << "ERROR: invalid mip_filter: " << settings.mip_filter
                   << "\n";
      return false;
    }
    tuning.mip_filter = *parsed;
  }

  if (!settings.mip_filter_space.empty()) {
    auto parsed = ParseColorSpace(settings.mip_filter_space);
    if (!parsed.has_value()) {
      error_stream << "ERROR: invalid mip_filter_space: "
                   << settings.mip_filter_space << "\n";
      return false;
    }
    tuning.mip_filter_space = *parsed;
  }

  const bool bc7_quality_set = !settings.bc7_quality.empty();
  if (bc7_quality_set) {
    auto parsed = ParseBc7Quality(settings.bc7_quality);
    if (!parsed.has_value()) {
      error_stream << "ERROR: invalid bc7_quality: " << settings.bc7_quality
                   << "\n";
      return false;
    }
    tuning.bc7_quality = *parsed;
  }

  if (!bc7_quality_set && !IsBc7Format(tuning.color_output_format)
    && !IsBc7Format(tuning.data_output_format)) {
    tuning.bc7_quality = Bc7Quality::kNone;
  }

  if (bc7_quality_set && tuning.bc7_quality != Bc7Quality::kNone
    && !IsBc7Format(tuning.color_output_format)
    && !IsBc7Format(tuning.data_output_format)) {
    error_stream << "ERROR: bc7_quality requires BC7 output_format or "
                    "data_format\n";
    return false;
  }

  if (!settings.packing_policy.empty()) {
    tuning.packing_policy_id = settings.packing_policy;
  }

  if (!settings.hdr_handling.empty()) {
    auto parsed = ParseHdrHandling(settings.hdr_handling);
    if (!parsed.has_value()) {
      error_stream << "ERROR: invalid hdr_handling: " << settings.hdr_handling
                   << "\n";
      return false;
    }
    tuning.hdr_handling = *parsed;
  }

  if (settings.exposure_ev != 0.0F) {
    tuning.exposure_ev = settings.exposure_ev;
  }

  if (settings.max_mip_levels > 0U) {
    tuning.max_mip_levels = static_cast<uint8_t>(settings.max_mip_levels);
  }

  if (settings.cubemap || settings.equirect_to_cube
    || !settings.cube_layout.empty()) {
    tuning.import_cubemap = true;
  }

  if (settings.equirect_to_cube) {
    tuning.equirect_to_cubemap = true;
    tuning.cubemap_face_size = settings.cube_face_size;
  }

  if (!settings.cube_layout.empty()) {
    auto parsed = ParseCubeLayout(settings.cube_layout);
    if (!parsed.has_value()) {
      error_stream << "ERROR: invalid cube_layout: " << settings.cube_layout
                   << "\n";
      return false;
    }
    tuning.cubemap_layout = *parsed;
  }

  tuning.flip_y_on_decode = settings.flip_y;
  if (!preset_applied || settings.force_rgba) {
    tuning.force_rgba_on_decode = settings.force_rgba;
  }
  tuning.flip_normal_green = settings.flip_normal_green;
  tuning.renormalize_normals_in_mips = settings.renormalize_normals;
  tuning.bake_hdr_to_ldr = settings.bake_hdr_to_ldr;

  return true;
}

} // namespace oxygen::content::import::internal
