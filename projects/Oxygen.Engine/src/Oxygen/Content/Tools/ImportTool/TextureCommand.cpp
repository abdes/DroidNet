//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iostream>

#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Content/Tools/ImportTool/ImportRunner.h>
#include <Oxygen/Content/Tools/ImportTool/TextureCommand.h>
#include <Oxygen/Content/Tools/ImportTool/TextureImportRequestBuilder.h>

namespace oxygen::content::import::tool {

namespace {

  using oxygen::clap::CommandBuilder;
  using oxygen::clap::Option;

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
    .WithOption(std::move(report));
}

auto TextureCommand::Run() -> int
{
  const auto request = BuildTextureRequest(options_, std::cerr);
  if (!request.has_value()) {
    return 2;
  }

  return RunImportJob(*request, options_.verbose, options_.report_path);
}

} // namespace oxygen::content::import::tool
