//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <optional>
#include <string>

#include <Oxygen/Content/Import/ImportOptions.h>
#include <Oxygen/Content/Import/TextureImportPresets.h>
#include <Oxygen/Content/Import/TextureImportTypes.h>
#include <Oxygen/Content/Import/TextureSourceAssembly.h>
#include <Oxygen/Content/Tools/ImportTool/TextureImportRequestBuilder.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>

namespace oxygen::content::import::tool {

namespace {

  using oxygen::ColorSpace;
  using oxygen::Format;
  using oxygen::TextureType;
  using oxygen::content::import::Bc7Quality;
  using oxygen::content::import::CubeMapImageLayout;
  using oxygen::content::import::MipFilter;
  using oxygen::content::import::MipPolicy;
  using oxygen::content::import::TextureImportDesc;
  using oxygen::content::import::TextureIntent;
  using oxygen::content::import::TexturePreset;

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

  auto IsBc7Format(const Format format) -> bool
  {
    return format == Format::kBC7UNorm || format == Format::kBC7UNormSRGB;
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
      return Format::kRGBA16Float;
    case TextureIntent::kData:
      return Format::kRGBA8UNorm;
    }
    return Format::kRGBA8UNorm;
  }

  struct PresetSelection {
    TexturePreset preset;
    std::optional<ColorSpace> source_color_space;
    std::optional<Format> output_format;
    std::optional<Bc7Quality> bc7_quality;
    std::optional<std::string> packing_policy;
  };

  auto ParsePreset(std::string_view value) -> std::optional<PresetSelection>
  {
    if (value == "albedo-srgb" || value == "albedo") {
      return PresetSelection { .preset = TexturePreset::kAlbedo };
    }
    if (value == "albedo-linear") {
      return PresetSelection { .preset = TexturePreset::kAlbedo,
        .source_color_space = ColorSpace::kLinear,
        .output_format = Format::kBC7UNorm };
    }
    if (value == "normal-linear") {
      return PresetSelection { .preset = TexturePreset::kNormal,
        .output_format = Format::kRGBA8UNorm,
        .bc7_quality = Bc7Quality::kNone };
    }
    if (value == "normal-bc7" || value == "normal") {
      return PresetSelection { .preset = TexturePreset::kNormal };
    }
    if (value == "orm-bc7" || value == "orm") {
      return PresetSelection { .preset = TexturePreset::kORMPacked };
    }
    if (value == "orm-tight") {
      return PresetSelection { .preset = TexturePreset::kORMPacked,
        .packing_policy = std::string("tight") };
    }
    if (value == "hdr-env-16f" || value == "hdr-env") {
      return PresetSelection { .preset = TexturePreset::kHdrEnvironment,
        .output_format = Format::kRGBA16Float };
    }
    if (value == "hdr-probe-16f" || value == "hdr-probe") {
      return PresetSelection { .preset = TexturePreset::kHdrLightProbe,
        .output_format = Format::kRGBA16Float };
    }
    if (value == "data-rgba8" || value == "data") {
      return PresetSelection { .preset = TexturePreset::kData,
        .output_format = Format::kRGBA8UNorm };
    }
    if (value == "data-rgba16f") {
      return PresetSelection { .preset = TexturePreset::kData,
        .output_format = Format::kRGBA16Float };
    }
    if (value == "height" || value == "height-16f") {
      return PresetSelection { .preset = TexturePreset::kHeightMap,
        .output_format = Format::kRGBA16Float };
    }
    return std::nullopt;
  }

  auto ApplyPresetToTuning(const PresetSelection& selection,
    ImportOptions::TextureTuning& tuning) -> void
  {
    TextureImportDesc desc;
    ApplyPreset(desc, selection.preset);
    tuning.intent = desc.intent;
    tuning.source_color_space = desc.source_color_space;
    tuning.mip_policy = desc.mip_policy;
    tuning.max_mip_levels = desc.max_mip_levels;
    tuning.mip_filter = desc.mip_filter;
    tuning.color_output_format = desc.output_format;
    tuning.data_output_format = desc.output_format;
    tuning.bc7_quality = desc.bc7_quality;
    tuning.flip_y_on_decode = desc.flip_y_on_decode;
    tuning.force_rgba_on_decode = desc.force_rgba_on_decode;
    if (desc.texture_type == TextureType::kTextureCube) {
      tuning.import_cubemap = true;
    }

    if (selection.source_color_space.has_value()) {
      tuning.source_color_space = *selection.source_color_space;
    }
    if (selection.output_format.has_value()) {
      tuning.color_output_format = *selection.output_format;
      tuning.data_output_format = *selection.output_format;
    }
    if (selection.bc7_quality.has_value()) {
      tuning.bc7_quality = *selection.bc7_quality;
    }
    if (selection.packing_policy.has_value()) {
      tuning.packing_policy_id = *selection.packing_policy;
    }
    tuning.enabled = true;
  }

} // namespace

auto BuildTextureRequest(const TextureImportSettings& settings,
  std::ostream& error_stream) -> std::optional<ImportRequest>
{
  ImportRequest request {};
  request.source_path = settings.source_path;

  if (settings.cooked_root.empty()) {
    error_stream << "ERROR: --output or --cooked-root is required\n";
    return std::nullopt;
  }

  if (!settings.cooked_root.empty()) {
    std::filesystem::path root(settings.cooked_root);
    if (!root.is_absolute()) {
      error_stream << "ERROR: cooked root must be an absolute path\n";
      return std::nullopt;
    }
    request.cooked_root = root;
  }
  if (!settings.job_name.empty()) {
    request.job_name = settings.job_name;
  } else {
    const auto stem = request.source_path.stem().string();
    if (!stem.empty()) {
      request.job_name = stem;
    }
  }

  auto& tuning = request.options.texture_tuning;

  bool preset_applied = false;
  if (!settings.preset.empty()) {
    const auto preset = ParsePreset(settings.preset);
    if (!preset.has_value()) {
      error_stream << "ERROR: invalid --preset value\n";
      return std::nullopt;
    }
    ApplyPresetToTuning(*preset, tuning);
    preset_applied = true;
  }

  const bool has_intent = !settings.intent.empty();
  if (!has_intent && !settings.preset.empty()) {
    tuning.enabled = true;
  }

  if (!settings.intent.empty()) {
    auto parsed = ParseIntent(settings.intent);
    if (!parsed.has_value()) {
      error_stream << "ERROR: invalid --intent value\n";
      return std::nullopt;
    }
    tuning.intent = *parsed;
    tuning.enabled = true;
  }

  if (!has_intent && settings.preset.empty()) {
    tuning.intent = TextureIntent::kData;
    tuning.enabled = true;
  }

  if (!settings.color_space.empty()) {
    auto parsed = ParseColorSpace(settings.color_space);
    if (!parsed.has_value()) {
      error_stream << "ERROR: invalid --color-space value\n";
      return std::nullopt;
    }
    tuning.source_color_space = *parsed;
    tuning.enabled = true;
  } else if (!preset_applied) {
    tuning.source_color_space = DefaultColorSpaceForIntent(tuning.intent);
    tuning.enabled = true;
  }

  if (!settings.output_format.empty()) {
    auto parsed = ParseFormat(settings.output_format);
    if (!parsed.has_value()) {
      error_stream << "ERROR: invalid --output-format value\n";
      return std::nullopt;
    }
    tuning.color_output_format = *parsed;
    if (settings.data_format.empty()) {
      tuning.data_output_format = *parsed;
    }
    tuning.enabled = true;
  } else if (!preset_applied) {
    const auto format = DefaultFormatForIntent(tuning.intent);
    tuning.color_output_format = format;
    tuning.data_output_format = format;
    tuning.enabled = true;
  }

  if (!settings.data_format.empty()) {
    if (tuning.intent != TextureIntent::kData) {
      error_stream << "ERROR: --data-format requires --intent=data\n";
      return std::nullopt;
    }
    auto parsed = ParseFormat(settings.data_format);
    if (!parsed.has_value()) {
      error_stream << "ERROR: invalid --data-format value\n";
      return std::nullopt;
    }
    tuning.data_output_format = *parsed;
    tuning.enabled = true;
  }

  if (!settings.mip_policy.empty()) {
    auto parsed = ParseMipPolicy(settings.mip_policy);
    if (!parsed.has_value()) {
      error_stream << "ERROR: invalid --mip-policy value\n";
      return std::nullopt;
    }
    tuning.mip_policy = *parsed;
    tuning.enabled = true;
  }

  if (!settings.mip_filter.empty()) {
    auto parsed = ParseMipFilter(settings.mip_filter);
    if (!parsed.has_value()) {
      error_stream << "ERROR: invalid --mip-filter value\n";
      return std::nullopt;
    }
    tuning.mip_filter = *parsed;
    tuning.enabled = true;
  }

  if (!settings.bc7_quality.empty()) {
    auto parsed = ParseBc7Quality(settings.bc7_quality);
    if (!parsed.has_value()) {
      error_stream << "ERROR: invalid --bc7-quality value\n";
      return std::nullopt;
    }
    tuning.bc7_quality = *parsed;
    tuning.enabled = true;
  }

  if (!settings.packing_policy.empty()) {
    tuning.packing_policy_id = settings.packing_policy;
    tuning.enabled = true;
  }

  if (settings.max_mip_levels > 0U) {
    if (tuning.mip_policy != MipPolicy::kMaxCount) {
      error_stream << "ERROR: --max-mips requires --mip-policy=max\n";
      return std::nullopt;
    }
    tuning.max_mip_levels = static_cast<uint8_t>(settings.max_mip_levels);
  }

  if (tuning.mip_policy == MipPolicy::kMaxCount
    && tuning.max_mip_levels == 0U) {
    error_stream << "ERROR: --max-mips must be > 0 when mip-policy=max\n";
    return std::nullopt;
  }

  if (!IsBc7Format(tuning.color_output_format)
    && !IsBc7Format(tuning.data_output_format)
    && !settings.bc7_quality.empty()) {
    error_stream << "ERROR: --bc7-quality requires bc7 output format\n";
    return std::nullopt;
  }

  if (settings.equirect_to_cube && !settings.cube_layout.empty()) {
    error_stream << "ERROR: --equirect-to-cube conflicts with --cube-layout\n";
    return std::nullopt;
  }

  if (settings.cube_face_size > 0U && !settings.equirect_to_cube) {
    error_stream << "ERROR: --cube-face-size requires --equirect-to-cube\n";
    return std::nullopt;
  }

  if (settings.equirect_to_cube && settings.cube_face_size == 0U) {
    error_stream
      << "ERROR: --cube-face-size must be > 0 for equirect conversion\n";
    return std::nullopt;
  }

  if (settings.cube_face_size > 0U && (settings.cube_face_size % 256U) != 0U) {
    error_stream << "ERROR: --cube-face-size must be a multiple of 256\n";
    return std::nullopt;
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
      error_stream << "ERROR: invalid --cube-layout value\n";
      return std::nullopt;
    }
    tuning.cubemap_layout = *parsed;
  }

  tuning.flip_y_on_decode = settings.flip_y;
  if (!preset_applied || settings.force_rgba) {
    tuning.force_rgba_on_decode = settings.force_rgba;
  }

  return request;
}

} // namespace oxygen::content::import::tool
