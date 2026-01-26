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
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/Internal/SceneImportRequestBuilder.h>
#include <Oxygen/Content/Tools/ImportTool/GltfCommand.h>
#include <Oxygen/Content/Tools/ImportTool/ImportRunner.h>
#include <Oxygen/Content/Tools/ImportTool/MessageWriter.h>

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
                      .CallOnEachValue([this](const float value) {
                        options_.unit_scale = value;
                        options_.unit_scale_set = true;
                      })
                      .Build();

  auto no_bake_transforms = Option::WithKey("no-bake-transforms")
                              .About("Disable transform baking into meshes")
                              .Long("no-bake-transforms")
                              .WithValue<bool>()
                              .StoreTo(&no_bake_transforms_)
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

  return CommandBuilder("gltf")
    .About("Import a standalone glTF/GLB scene")
    .WithPositionalArguments(source_path)
    .WithOption(std::move(cooked_root))
    .WithOption(std::move(job_name))
    .WithOption(std::move(report))
    .WithOption(std::move(no_import_textures))
    .WithOption(std::move(no_import_materials))
    .WithOption(std::move(no_import_geometry))
    .WithOption(std::move(no_import_scene))
    .WithOption(std::move(unit_policy))
    .WithOption(std::move(unit_scale))
    .WithOption(std::move(no_bake_transforms))
    .WithOption(std::move(normals))
    .WithOption(std::move(tangents))
    .WithOption(std::move(prune_nodes))
    .WithOption(std::move(cooked_root_alias))
    .WithOption(std::move(with_content_hashing));
}

auto GltfCommand::Run() -> std::expected<void, std::error_code>
{
  auto settings = options_;
  DCHECK_F(global_options_ != nullptr && global_options_->writer,
    "Global message writer must be set by main");
  auto writer = global_options_->writer;

  settings.import_textures = !no_import_textures_;
  settings.import_materials = !no_import_materials_;
  settings.import_geometry = !no_import_geometry_;
  settings.import_scene = !no_import_scene_;
  settings.bake_transforms = !no_bake_transforms_;

  std::optional<ImportRequest> request;
  {
    std::ostringstream err;
    request = internal::BuildSceneRequest(settings, ImportFormat::kGltf, err);
    if (!request.has_value()) {
      const auto msg = err.str();
      if (!msg.empty()) {
        writer->Error(msg);
      }
      return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
  }

  return RunImportJob(*request, writer, settings.report_path,
    global_options_->command_line, !global_options_->no_tui,
    global_options_->import_service);
}

} // namespace oxygen::content::import::tool
