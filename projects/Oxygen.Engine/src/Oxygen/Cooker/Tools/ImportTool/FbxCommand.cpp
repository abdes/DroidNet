//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

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
#include <Oxygen/Cooker/Tools/ImportTool/FbxCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/ImportRunner.h>

namespace oxygen::content::import::tool {

namespace {

  using oxygen::clap::CommandBuilder;
  using oxygen::clap::Option;
  using oxygen::content::import::ImportFormat;

} // namespace

auto FbxCommand::Name() const -> std::string_view { return "fbx"; }

auto FbxCommand::BuildCommand() -> std::shared_ptr<clap::Command>
{
  auto source_path = Option::Positional("source")
                       .About("Path to the source FBX file")
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
                  .About("Authored retained import record; omit source to "
                         "replay its saved recipe")
                  .WithValue<std::string>()
                  .StoreTo(&options_.retained_record_path)
                  .Build();
  auto content_root
    = Option::WithKey("content-root")
        .Long("content-root")
        .About(
          "Owned Content area for retained records and immutable generations")
        .WithValue<std::string>()
        .StoreTo(&options_.content_root)
        .Build();

  auto source_identity
    = Option::WithKey("material-slot-source-identity")
        .Long("material-slot-source-identity")
        .About("Retained UUIDv7 namespace; required when importing geometry")
        .WithValue<std::string>()
        .StoreTo(&options_.material_slot_source_identity)
        .Build();
  auto provenance = Option::WithKey("material-slot-provenance")
                      .Long("material-slot-provenance")
                      .About("JSON file containing retained material-slot "
                             "provenance from an earlier import")
                      .WithValue<std::string>()
                      .StoreTo(&options_.material_slot_provenance_path)
                      .Build();

  auto recipe
    = Option::WithKey("recipe")
        .About("Apply a single-job native manifest to a retained record")
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

  auto no_bake_transforms = Option::WithKey("no-bake-transforms")
                              .About("Disable transform baking into meshes")
                              .Long("no-bake-transforms")
                              .WithValue<bool>()
                              .StoreTo(&no_bake_transforms_)
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

  return CommandBuilder("fbx")
    .About("Import a standalone FBX scene")
    .WithPositionalArguments(source_path)
    .WithOption(std::move(cooked_root))
    .WithOption(std::move(job_name))
    .WithOption(std::move(report))
    .WithOption(std::move(record))
    .WithOption(std::move(recipe))
    .WithOption(std::move(content_root))
    .WithOption(std::move(source_identity))
    .WithOption(std::move(provenance))
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
    .WithOption(std::move(with_content_hashing));
}

auto FbxCommand::Run() -> std::expected<void, std::error_code>
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
  settings.bake_transforms = !no_bake_transforms_;

  return RunSceneImportJob(settings, ImportFormat::kFbx, *global_options_);
}

} // namespace oxygen::content::import::tool
