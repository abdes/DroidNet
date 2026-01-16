//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>

#include <Oxygen/Content/Import/ImportOptions.h>
#include <Oxygen/Content/Import/TextureImportTypes.h>
#include <Oxygen/Content/Import/TextureSourceAssembly.h>
#include <Oxygen/Content/Tools/ImportTool/TextureImportRequestBuilder.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/Core/Types/Format.h>

namespace oxygen::content::import::tool {

namespace {

  using oxygen::ColorSpace;
  using oxygen::Format;
  using oxygen::content::import::Bc7Quality;
  using oxygen::content::import::CubeMapImageLayout;
  using oxygen::content::import::MipFilter;
  using oxygen::content::import::MipPolicy;
  using oxygen::content::import::TextureIntent;

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
    if (value == "hdr_env") {
      return TextureIntent::kHdrEnvironment;
    }
    if (value == "hdr_probe") {
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
    if (value == "rgba8_srgb") {
      return Format::kRGBA8UNormSRGB;
    }
    if (value == "bc7") {
      return Format::kBC7UNorm;
    }
    if (value == "bc7_srgb") {
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

} // namespace

auto BuildTextureRequest(const TextureImportSettings& settings,
  std::ostream& error_stream) -> std::optional<ImportRequest>
{
  ImportRequest request {};
  request.source_path = settings.source_path;

  if (!settings.cooked_root.empty()) {
    request.cooked_root = std::filesystem::path(settings.cooked_root);
  }
  if (!settings.job_name.empty()) {
    request.job_name = settings.job_name;
  }

  auto& tuning = request.options.texture_tuning;

  if (!settings.intent.empty()) {
    auto parsed = ParseIntent(settings.intent);
    if (!parsed.has_value()) {
      error_stream << "ERROR: invalid --intent value\n";
      return std::nullopt;
    }
    tuning.intent = *parsed;
  }

  if (!settings.color_space.empty()) {
    auto parsed = ParseColorSpace(settings.color_space);
    if (!parsed.has_value()) {
      error_stream << "ERROR: invalid --color-space value\n";
      return std::nullopt;
    }
    tuning.source_color_space = *parsed;
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
  }

  if (!settings.data_format.empty()) {
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
    tuning.max_mip_levels = static_cast<uint8_t>(settings.max_mip_levels);
  }

  if (tuning.mip_policy == MipPolicy::kMaxCount
    && tuning.max_mip_levels == 0U) {
    error_stream << "ERROR: --max-mips must be > 0 when mip-policy=max\n";
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
  tuning.force_rgba_on_decode = settings.force_rgba;

  return request;
}

} // namespace oxygen::content::import::tool
