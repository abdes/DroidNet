//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iostream>
#include <sstream>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Content/Import/Internal/TextureImportRequestBuilder.h>
#include <Oxygen/Content/Tools/ImportTool/ImportRunner.h>
#include <Oxygen/Content/Tools/ImportTool/MessageWriter.h>
#include <Oxygen/Content/Tools/ImportTool/TextureCommand.h>

namespace oxygen::content::import::tool {

namespace {

  using oxygen::clap::CommandBuilder;
  using oxygen::clap::Option;

  auto ParseSourceMapping(const std::string& input)
    -> std::optional<TextureSourceMapping>
  {
    // Format: file[:layer[:mip[:slice]]]
    TextureSourceMapping mapping;
    auto parts = std::vector<std::string>();
    size_t start = 0;
    size_t end = input.find(':');
    while (end != std::string::npos) {
      parts.push_back(input.substr(start, end - start));
      start = end + 1;
      end = input.find(':', start);
    }
    parts.push_back(input.substr(start));

    if (parts.empty() || parts[0].empty()) {
      return std::nullopt;
    }

    mapping.file = parts[0];
    try {
      if (parts.size() > 1) {
        mapping.layer = static_cast<uint16_t>(std::stoul(parts[1]));
      }
      if (parts.size() > 2) {
        mapping.mip = static_cast<uint16_t>(std::stoul(parts[2]));
      }
      if (parts.size() > 3) {
        mapping.slice = static_cast<uint16_t>(std::stoul(parts[3]));
      }
    } catch (...) {
      return std::nullopt;
    }
    return mapping;
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

  // Alias to match global option name; accepts --cooked-root after the
  // subcommand
  auto cooked_root_alias = Option::WithKey("cooked-root")
                             .About("Destination cooked root directory")
                             .Long("cooked-root")
                             .WithValue<std::string>()
                             .StoreTo(&options_.cooked_root)
                             .Build();

  auto with_content_hashing
    = Option::WithKey("content-hashing")
        .About("Enable or disable content hashing for outputs")
        .Long("content-hashing")
        .WithValue<bool>()
        .StoreTo(&options_.with_content_hashing)
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

  auto preset = Option::WithKey("preset")
                  .About("Apply a named texture preset")
                  .Long("preset")
                  .WithValue<std::string>()
                  .StoreTo(&options_.preset)
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

  auto mip_filter_space
    = Option::WithKey("mip-filter-space")
        .About("Color space for mip filtering (srgb, linear)")
        .Long("mip-filter-space")
        .WithValue<std::string>()
        .StoreTo(&options_.mip_filter_space)
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

  auto hdr_handling = Option::WithKey("hdr-handling")
                        .About("HDR handling: error, tonemap, keep")
                        .Long("hdr-handling")
                        .WithValue<std::string>()
                        .StoreTo(&options_.hdr_handling)
                        .Build();

  auto exposure_ev = Option::WithKey("exposure-ev")
                       .About("Exposure adjustment in EV")
                       .Long("exposure-ev")
                       .WithValue<float>()
                       .StoreTo(&options_.exposure_ev)
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

  auto flip_normal_green = Option::WithKey("flip-normal-green")
                             .About("Flip the green channel for normal maps")
                             .Long("flip-normal-green")
                             .WithValue<bool>()
                             .StoreTo(&options_.flip_normal_green)
                             .Build();

  auto renormalize_normals = Option::WithKey("renormalize-normals")
                               .About("Renormalize normals in mip levels")
                               .Long("renormalize-normals")
                               .WithValue<bool>()
                               .StoreTo(&options_.renormalize_normals)
                               .Build();

  auto bake_hdr_to_ldr = Option::WithKey("bake-hdr-to-ldr")
                           .About("Bake HDR content to LDR via tonemap")
                           .Long("bake-hdr-to-ldr")
                           .WithValue<bool>()
                           .StoreTo(&options_.bake_hdr_to_ldr)
                           .Build();

  auto sources = Option::WithKey("source")
                   .About("Additional source mapping (file:layer:mip:slice)")
                   .Long("source")
                   .WithValue<std::string>()
                   .Repeatable()
                   .CallOnEachValue([this](const std::string& val) {
                     if (auto mapping = ParseSourceMapping(val)) {
                       options_.sources.push_back(std::move(*mapping));
                     }
                   })
                   .Build();

  auto report = Option::WithKey("report")
                  .About("Write a JSON report (absolute or relative to cooked "
                         "root)")
                  .Long("report")
                  .WithValue<std::string>()
                  .StoreTo(&options_.report_path)
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
    .WithOption(std::move(preset))
    .WithOption(std::move(mip_policy))
    .WithOption(std::move(max_mips))
    .WithOption(std::move(mip_filter))
    .WithOption(std::move(mip_filter_space))
    .WithOption(std::move(bc7_quality))
    .WithOption(std::move(packing_policy))
    .WithOption(std::move(hdr_handling))
    .WithOption(std::move(exposure_ev))
    .WithOption(std::move(cubemap))
    .WithOption(std::move(equirect_to_cube))
    .WithOption(std::move(cube_face_size))
    .WithOption(std::move(cube_layout))
    .WithOption(std::move(flip_y))
    .WithOption(std::move(force_rgba))
    .WithOption(std::move(flip_normal_green))
    .WithOption(std::move(renormalize_normals))
    .WithOption(std::move(bake_hdr_to_ldr))
    .WithOption(std::move(sources))
    .WithOption(std::move(report))
    .WithOption(std::move(cooked_root_alias))
    .WithOption(std::move(with_content_hashing));
}

auto TextureCommand::Run() -> std::expected<void, std::error_code>
{
  auto settings = options_;

  if (global_options_ != nullptr && settings.cooked_root.empty()) {
    settings.cooked_root = global_options_->cooked_root;
  }

  DCHECK_F(global_options_ != nullptr && global_options_->writer,
    "Global message writer must be set by main");
  auto writer = global_options_->writer;

  std::ostringstream err;
  const auto request = internal::BuildTextureRequest(settings, err);
  if (!request.has_value()) {
    const auto msg = err.str();
    if (!msg.empty()) {
      writer->Error(msg);
    }
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  return RunImportJob(*request, writer, settings.report_path,
    global_options_->command_line, !global_options_->no_tui,
    global_options_->import_service);
}

} // namespace oxygen::content::import::tool
