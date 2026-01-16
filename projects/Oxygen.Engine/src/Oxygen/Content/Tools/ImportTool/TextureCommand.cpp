//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Tools/ImportTool/TextureCommand.h>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>

#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Content/Import/ImportOptions.h>
#include <Oxygen/Content/Import/TextureImportTypes.h>
#include <Oxygen/Content/Import/TextureSourceAssembly.h>
#include <Oxygen/Content/Tools/ImportTool/ImportRunner.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/Core/Types/Format.h>

namespace oxygen::content::import::tool {

namespace {

  using oxygen::ColorSpace;
  using oxygen::Format;
  using oxygen::clap::CommandBuilder;
  using oxygen::clap::Option;
  using oxygen::content::import::Bc7Quality;
  using oxygen::content::import::CubeMapImageLayout;
  using oxygen::content::import::ImportRequest;
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

  auto BuildTextureRequest(const TextureCommand::Options& opts)
    -> std::optional<ImportRequest>
  {
    ImportRequest request {};
    request.source_path = opts.source_path;

    if (!opts.cooked_root.empty()) {
      request.cooked_root = std::filesystem::path(opts.cooked_root);
    }
    if (!opts.job_name.empty()) {
      request.job_name = opts.job_name;
    }

    auto& tuning = request.options.texture_tuning;

    if (!opts.intent.empty()) {
      auto parsed = ParseIntent(opts.intent);
      if (!parsed.has_value()) {
        std::cerr << "ERROR: invalid --intent value\n";
        return std::nullopt;
      }
      tuning.intent = *parsed;
    }

    if (!opts.color_space.empty()) {
      auto parsed = ParseColorSpace(opts.color_space);
      if (!parsed.has_value()) {
        std::cerr << "ERROR: invalid --color-space value\n";
        return std::nullopt;
      }
      tuning.source_color_space = *parsed;
    }

    if (!opts.output_format.empty()) {
      auto parsed = ParseFormat(opts.output_format);
      if (!parsed.has_value()) {
        std::cerr << "ERROR: invalid --output-format value\n";
        return std::nullopt;
      }
      tuning.color_output_format = *parsed;
      if (opts.data_format.empty()) {
        tuning.data_output_format = *parsed;
      }
      tuning.enabled = true;
    }

    if (!opts.data_format.empty()) {
      auto parsed = ParseFormat(opts.data_format);
      if (!parsed.has_value()) {
        std::cerr << "ERROR: invalid --data-format value\n";
        return std::nullopt;
      }
      tuning.data_output_format = *parsed;
      tuning.enabled = true;
    }

    if (!opts.mip_policy.empty()) {
      auto parsed = ParseMipPolicy(opts.mip_policy);
      if (!parsed.has_value()) {
        std::cerr << "ERROR: invalid --mip-policy value\n";
        return std::nullopt;
      }
      tuning.mip_policy = *parsed;
      tuning.enabled = true;
    }

    if (!opts.mip_filter.empty()) {
      auto parsed = ParseMipFilter(opts.mip_filter);
      if (!parsed.has_value()) {
        std::cerr << "ERROR: invalid --mip-filter value\n";
        return std::nullopt;
      }
      tuning.mip_filter = *parsed;
      tuning.enabled = true;
    }

    if (!opts.bc7_quality.empty()) {
      auto parsed = ParseBc7Quality(opts.bc7_quality);
      if (!parsed.has_value()) {
        std::cerr << "ERROR: invalid --bc7-quality value\n";
        return std::nullopt;
      }
      tuning.bc7_quality = *parsed;
      tuning.enabled = true;
    }

    if (!opts.packing_policy.empty()) {
      tuning.packing_policy_id = opts.packing_policy;
      tuning.enabled = true;
    }

    if (opts.max_mip_levels > 0U) {
      tuning.max_mip_levels = static_cast<uint8_t>(opts.max_mip_levels);
    }

    if (tuning.mip_policy == MipPolicy::kMaxCount
      && tuning.max_mip_levels == 0U) {
      std::cerr << "ERROR: --max-mips must be > 0 when mip-policy=max\n";
      return std::nullopt;
    }

    if (opts.equirect_to_cube && !opts.cube_layout.empty()) {
      std::cerr << "ERROR: --equirect-to-cube conflicts with --cube-layout\n";
      return std::nullopt;
    }

    if (opts.cube_face_size > 0U && !opts.equirect_to_cube) {
      std::cerr << "ERROR: --cube-face-size requires --equirect-to-cube\n";
      return std::nullopt;
    }

    if (opts.equirect_to_cube && opts.cube_face_size == 0U) {
      std::cerr
        << "ERROR: --cube-face-size must be > 0 for equirect conversion\n";
      return std::nullopt;
    }

    if (opts.cube_face_size > 0U && (opts.cube_face_size % 256U) != 0U) {
      std::cerr << "ERROR: --cube-face-size must be a multiple of 256\n";
      return std::nullopt;
    }

    if (opts.cubemap || opts.equirect_to_cube || !opts.cube_layout.empty()) {
      tuning.import_cubemap = true;
    }

    if (opts.equirect_to_cube) {
      tuning.equirect_to_cubemap = true;
      tuning.cubemap_face_size = opts.cube_face_size;
    }

    if (!opts.cube_layout.empty()) {
      auto parsed = ParseCubeLayout(opts.cube_layout);
      if (!parsed.has_value()) {
        std::cerr << "ERROR: invalid --cube-layout value\n";
        return std::nullopt;
      }
      tuning.cubemap_layout = *parsed;
    }

    tuning.flip_y_on_decode = opts.flip_y;
    tuning.force_rgba_on_decode = opts.force_rgba;

    return request;
  }

} // namespace

auto TextureCommand::Name() const -> std::string_view { return "texture"; }

auto TextureCommand::BuildCommand() -> std::shared_ptr<clap::Command>
{
  auto source_path = Option::Positional("source")
                       .About("Path to the source image file")
                       .Required()
                       .WithValue<std::string>()
                       .StoreTo(&options_.source_path)
                       .Build();

  auto cooked_root = Option::WithKey("output")
                       .About("Destination cooked root directory")
                       .Short("o")
                       .Long("output")
                       .WithValue<std::string>()
                       .StoreTo(&options_.cooked_root)
                       .Build();

  auto job_name = Option::WithKey("name")
                    .About("Optional job name")
                    .Long("name")
                    .WithValue<std::string>()
                    .StoreTo(&options_.job_name)
                    .Build();

  auto intent
    = Option::WithKey("intent")
        .About("Texture intent: albedo, normal, roughness, metallic, "
               "ao, emissive, opacity, orm, hdr_env, hdr_probe, data, "
               "height")
        .Long("intent")
        .WithValue<std::string>()
        .StoreTo(&options_.intent)
        .Build();

  auto color_space = Option::WithKey("color-space")
                       .About("Source color space (srgb or linear)")
                       .Long("color-space")
                       .WithValue<std::string>()
                       .StoreTo(&options_.color_space)
                       .Build();

  auto output_format = Option::WithKey("output-format")
                         .About("Output format: rgba8, rgba8_srgb, bc7, "
                                "bc7_srgb, rgba16f, rgba32f")
                         .Long("output-format")
                         .WithValue<std::string>()
                         .StoreTo(&options_.output_format)
                         .Build();

  auto data_format = Option::WithKey("data-format")
                       .About("Data format for non-color intents")
                       .Long("data-format")
                       .WithValue<std::string>()
                       .StoreTo(&options_.data_format)
                       .Build();

  auto mip_policy = Option::WithKey("mip-policy")
                      .About("Mip policy (none, full, max)")
                      .Long("mip-policy")
                      .WithValue<std::string>()
                      .StoreTo(&options_.mip_policy)
                      .Build();

  auto max_mips = Option::WithKey("max-mips")
                    .About("Max mip levels (when mip-policy=max)")
                    .Long("max-mips")
                    .WithValue<uint32_t>()
                    .StoreTo(&options_.max_mip_levels)
                    .Build();

  auto mip_filter = Option::WithKey("mip-filter")
                      .About("Mip filter (box, kaiser, lanczos)")
                      .Long("mip-filter")
                      .WithValue<std::string>()
                      .StoreTo(&options_.mip_filter)
                      .Build();

  auto bc7_quality = Option::WithKey("bc7-quality")
                       .About("BC7 quality (none, fast, default, high)")
                       .Long("bc7-quality")
                       .WithValue<std::string>()
                       .StoreTo(&options_.bc7_quality)
                       .Build();

  auto packing_policy = Option::WithKey("packing-policy")
                          .About("Packing policy (d3d12 or tight)")
                          .Long("packing-policy")
                          .WithValue<std::string>()
                          .StoreTo(&options_.packing_policy)
                          .Build();

  auto cubemap = Option::WithKey("cubemap")
                   .About("Import as a cubemap")
                   .Long("cubemap")
                   .WithValue<bool>()
                   .StoreTo(&options_.cubemap)
                   .Build();

  auto equirect_to_cube = Option::WithKey("equirect-to-cube")
                            .About("Convert equirectangular panorama to cube")
                            .Long("equirect-to-cube")
                            .WithValue<bool>()
                            .StoreTo(&options_.equirect_to_cube)
                            .Build();

  auto cube_face_size = Option::WithKey("cube-face-size")
                          .About("Cubemap face size in pixels")
                          .Long("cube-face-size")
                          .WithValue<uint32_t>()
                          .StoreTo(&options_.cube_face_size)
                          .Build();

  auto cube_layout = Option::WithKey("cube-layout")
                       .About("Cubemap layout: auto, hstrip, vstrip, hcross, "
                              "vcross")
                       .Long("cube-layout")
                       .WithValue<std::string>()
                       .StoreTo(&options_.cube_layout)
                       .Build();

  auto flip_y = Option::WithKey("flip-y")
                  .About("Flip image vertically during decode")
                  .Long("flip-y")
                  .WithValue<bool>()
                  .StoreTo(&options_.flip_y)
                  .Build();

  auto force_rgba = Option::WithKey("force-rgba")
                      .About("Force RGBA output during decode")
                      .Long("force-rgba")
                      .WithValue<bool>()
                      .StoreTo(&options_.force_rgba)
                      .Build();

  auto verbose = Option::WithKey("verbose")
                   .About("Print progress updates")
                   .Short("v")
                   .Long("verbose")
                   .WithValue<bool>()
                   .StoreTo(&options_.verbose)
                   .Build();

  auto print_telemetry = Option::WithKey("print-telemetry")
                           .About("Print telemetry timing after completion")
                           .Long("print-telemetry")
                           .WithValue<bool>()
                           .StoreTo(&options_.print_telemetry)
                           .Build();

  return CommandBuilder("texture")
    .About("Import a standalone texture image")
    .WithPositionalArguments(source_path)
    .WithOption(std::move(cooked_root))
    .WithOption(std::move(job_name))
    .WithOption(std::move(intent))
    .WithOption(std::move(color_space))
    .WithOption(std::move(output_format))
    .WithOption(std::move(data_format))
    .WithOption(std::move(mip_policy))
    .WithOption(std::move(max_mips))
    .WithOption(std::move(mip_filter))
    .WithOption(std::move(bc7_quality))
    .WithOption(std::move(packing_policy))
    .WithOption(std::move(cubemap))
    .WithOption(std::move(equirect_to_cube))
    .WithOption(std::move(cube_face_size))
    .WithOption(std::move(cube_layout))
    .WithOption(std::move(flip_y))
    .WithOption(std::move(force_rgba))
    .WithOption(std::move(verbose))
    .WithOption(std::move(print_telemetry));
}

auto TextureCommand::Run() -> int
{
  const auto request = BuildTextureRequest(options_);
  if (!request.has_value()) {
    return 2;
  }

  return RunImportJob(*request, options_.verbose, options_.print_telemetry);
}

} // namespace oxygen::content::import::tool
