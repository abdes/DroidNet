//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Tools/ImportTool/GltfCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/ImportRunner.h>

namespace oxygen::content::import::tool {

namespace {

  using oxygen::clap::CommandBuilder;
  using oxygen::clap::Option;
  using oxygen::content::import::ImportFormat;

} // namespace

auto GltfCommand::Name() const -> std::string_view { return "gltf"; }

auto GltfCommand::BuildCommand() -> std::shared_ptr<clap::Command>
{
  auto source_path = Option::Positional("source")
                       .About("Path to the source glTF/GLB file")
                       .WithValue<std::string>()
                       .DefaultValue(std::string {})
                       .StoreTo(&options_.source_path)
                       .Build();

  auto cooked_root = Option::WithKey("output")
                       .About("Destination cooked root directory")
                       .Short("i")
                       .Long("output")
                       .WithValue<std::string>()
                       .StoreTo(&options_.cooked_root)
                       .Build();

  auto virtual_path
    = Option::WithKey("virtual-path")
        .About("Identity of the model: the virtual path of its source file, "
               "under which its products are named")
        .Long("virtual-path")
        .WithValue<std::string>()
        .StoreTo(&options_.virtual_path)
        .Build();

  auto job_name = Option::WithKey("name")
                    .About("Optional job name")
                    .Long("name")
                    .WithValue<std::string>()
                    .StoreTo(&options_.job_name)
                    .Build();

  auto report
    = Option::WithKey("report")
        .About("Write a JSON report (absolute or relative to cooked root)")
        .Long("report")
        .WithValue<std::string>()
        .StoreTo(&options_.report_path)
        .Build();

  auto no_import_textures = Option::WithKey("no-import-textures")
                              .About("Disable texture import")
                              .Long("no-import-textures")
                              .WithValue<bool>()
                              .StoreTo(&no_import_textures_)
                              .Build();

  auto record = Option::WithKey("record")
                  .Long("record")
                  .About("Model import record; omit source to replay its saved "
                         "settings")
                  .WithValue<std::string>()
                  .StoreTo(&options_.model_record_path)
                  .Build();
  auto content_root = Option::WithKey("content-root")
                        .Long("content-root")
                        .About("Owned Content area for model import records "
                               "and immutable generations")
                        .WithValue<std::string>()
                        .StoreTo(&options_.content_root)
                        .Build();

  auto recipe
    = Option::WithKey("recipe")
        .About("Apply a single-job native manifest to a model import record")
        .Long("recipe")
        .WithValue<std::string>()
        .StoreTo(&options_.recipe_path)
        .Build();

  auto no_import_materials = Option::WithKey("no-import-materials")
                               .About("Disable material import")
                               .Long("no-import-materials")
                               .WithValue<bool>()
                               .StoreTo(&no_import_materials_)
                               .Build();

  auto no_import_geometry = Option::WithKey("no-import-geometry")
                              .About("Disable geometry import")
                              .Long("no-import-geometry")
                              .WithValue<bool>()
                              .StoreTo(&no_import_geometry_)
                              .Build();

  auto no_import_scene = Option::WithKey("no-import-scene")
                           .About("Disable scene import")
                           .Long("no-import-scene")
                           .WithValue<bool>()
                           .StoreTo(&no_import_scene_)
                           .Build();

  auto unit_policy = Option::WithKey("unit-policy")
                       .About("Unit policy (normalize, preserve, custom)")
                       .Long("unit-policy")
                       .WithValue<std::string>()
                       .StoreTo(&options_.unit_policy)
                       .Build();

  auto unit_scale = Option::WithKey("unit-scale")
                      .About("Custom unit scale when unit-policy=custom")
                      .Long("unit-scale")
                      .WithValue<float>()
                      .CallOnEachValue([this](const float value) -> void {
                        options_.unit_scale = value;
                        options_.unit_scale_set = true;
                      })
                      .Build();

  auto omitted_light_range
    = Option::WithKey("omitted-light-range")
        .About("Range in meters for glTF lights without a range (default 4096)")
        .Long("omitted-light-range")
        .WithValue<float>()
        .StoreTo(&options_.gltf_omitted_light_range_m)
        .Build();

  auto gltf_scene
    = Option::WithKey("gltf-scene")
        .About("The scene imported as the model scene: its name, or #N for the "
               "scene at index N (default: the file's default scene)")
        .Long("gltf-scene")
        .WithValue<std::string>()
        .StoreTo(&options_.gltf_scene)
        .Build();

  auto bake_transforms = Option::WithKey("bake-transforms")
                           .About("Bake node transforms into meshes, moving "
                                  "every pivot to the model's origin")
                           .Long("bake-transforms")
                           .WithValue<bool>()
                           .StoreTo(&bake_transforms_)
                           .Build();

  auto with_content_hashing
    = Option::WithKey("content-hashing")
        .About("Enable or disable content hashing for outputs")
        .Long("content-hashing")
        .WithValue<bool>()
        .StoreTo(&options_.with_content_hashing)
        .Build();

  auto normals
    = Option::WithKey("normals")
        .About("Normals policy (none, preserve, generate, recalculate)")
        .Long("normals")
        .WithValue<std::string>()
        .StoreTo(&options_.normals_policy)
        .Build();

  auto tangents
    = Option::WithKey("tangents")
        .About("Tangents policy (none, preserve, generate, recalculate)")
        .Long("tangents")
        .WithValue<std::string>()
        .StoreTo(&options_.tangents_policy)
        .Build();

  auto prune_nodes = Option::WithKey("prune-nodes")
                       .About("Node pruning policy (keep, drop-empty)")
                       .Long("prune-nodes")
                       .WithValue<std::string>()
                       .StoreTo(&options_.node_pruning)
                       .Build();

  auto smoothing_angle
    = Option::WithKey("smoothing-angle")
        .About("Largest angle in degrees between faces that share a generated "
               "normal (default 60)")
        .Long("smoothing-angle")
        .WithValue<float>()
        .StoreTo(&options_.normals_smoothing_angle_deg)
        .Build();

  auto no_import_lights = Option::WithKey("no-import-lights")
                            .About("Disable light import")
                            .Long("no-import-lights")
                            .WithValue<bool>()
                            .StoreTo(&no_import_lights_)
                            .Build();

  auto no_import_cameras = Option::WithKey("no-import-cameras")
                             .About("Disable camera import")
                             .Long("no-import-cameras")
                             .WithValue<bool>()
                             .StoreTo(&no_import_cameras_)
                             .Build();

  auto light_intensity_scale
    = Option::WithKey("light-intensity-scale")
        .About("Factor applied to every imported light's intensity (default 1)")
        .Long("light-intensity-scale")
        .WithValue<float>()
        .StoreTo(&options_.light_intensity_scale)
        .Build();

  auto max_texture_size
    = Option::WithKey("max-texture-size")
        .About("Largest stored texture width or height (default: source size)")
        .Long("max-texture-size")
        .WithValue<uint32_t>()
        .StoreTo(&options_.texture_defaults.max_size)
        .Build();

  return CommandBuilder("gltf")
    .About("Import a standalone glTF/GLB scene")
    .WithPositionalArguments(source_path)
    .WithOption(std::move(cooked_root))
    .WithOption(std::move(virtual_path))
    .WithOption(std::move(job_name))
    .WithOption(std::move(report))
    .WithOption(std::move(record))
    .WithOption(std::move(recipe))
    .WithOption(std::move(content_root))
    .WithOption(std::move(no_import_textures))
    .WithOption(std::move(no_import_materials))
    .WithOption(std::move(no_import_geometry))
    .WithOption(std::move(no_import_scene))
    .WithOption(std::move(unit_policy))
    .WithOption(std::move(unit_scale))
    .WithOption(std::move(omitted_light_range))
    .WithOption(std::move(gltf_scene))
    .WithOption(std::move(bake_transforms))
    .WithOption(std::move(normals))
    .WithOption(std::move(tangents))
    .WithOption(std::move(prune_nodes))
    .WithOption(std::move(smoothing_angle))
    .WithOption(std::move(no_import_lights))
    .WithOption(std::move(no_import_cameras))
    .WithOption(std::move(light_intensity_scale))
    .WithOption(std::move(max_texture_size))
    .WithOption(std::move(with_content_hashing));
}

auto GltfCommand::Run() -> std::expected<void, std::error_code>
{
  auto settings = options_;
  if (global_options_ != nullptr && settings.cooked_root.empty()) {
    settings.cooked_root = global_options_->cooked_root;
  }
  DCHECK_F(global_options_ != nullptr && global_options_->writer,
    "Global message writer must be set by main");

  settings.import_textures = !no_import_textures_;
  settings.import_materials = !no_import_materials_;
  settings.import_geometry = !no_import_geometry_;
  settings.import_scene = !no_import_scene_;
  settings.bake_transforms = bake_transforms_;
  settings.import_lights = !no_import_lights_;
  settings.import_cameras = !no_import_cameras_;

  return RunSceneImportJob(settings, ImportFormat::kGltf, *global_options_);
}

} // namespace oxygen::content::import::tool
