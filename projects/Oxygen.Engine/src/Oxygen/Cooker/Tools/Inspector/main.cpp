//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <Oxygen/Cooker/Tools/Common/ToolApp.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/Tools/Inspector/AssetKeyMap.h>
#include <Oxygen/Cooker/Tools/Inspector/ContainerCommands.h>
#include <Oxygen/Cooker/Tools/Inspector/DependencyReport.h>
#include <Oxygen/Cooker/Tools/Inspector/DescriptorCommands.h>
#include <Oxygen/Cooker/Tools/Inspector/GeometryMetadata.h>
#include <Oxygen/Cooker/Tools/Inspector/InventoryReport.h>
#include <Oxygen/Cooker/Tools/Inspector/ReportCommand.h>
#include <Oxygen/Cooker/Tools/Inspector/ResourceTableCommands.h>
#include <Oxygen/Cooker/Tools/Inspector/SceneMetadata.h>
#include <Oxygen/Cooker/Tools/Inspector/ScriptMetadata.h>

namespace oxygen::content::inspection {

namespace {

  auto RunInspector(int argc, char** argv) -> int
  {
    using oxygen::content::toolkit::RunToolMain;
    using oxygen::content::toolkit::ToolCommand;
    using oxygen::content::toolkit::ToolInfo;

    ValidateCommand validate;
    IndexCommand index;
    const auto buffers = MakeBuffersCommand();
    const auto textures = MakeTexturesCommand();
    const auto physics_table = MakePhysicsTableCommand();
    const auto script_slots = MakeScriptSlotsCommand();
    const auto script_params = MakeScriptParametersCommand();
    const auto input_actions = MakeInputActionsCommand();
    const auto input_mappings = MakeInputMappingsCommand();
    const auto physics = MakePhysicsAssetsCommand();
    ReportCommand<DependencyReportOptions> dependencies({
      .name = "dependencies",
      .build = BuildDependencyCommand,
      .run = RunDependencyReport,
    });
    ReportCommand<AssetKeyMapOptions> asset_keys({
      .name = "asset-keys",
      .build = BuildAssetKeyMapCommand,
      .run = RunAssetKeyMap,
    });
    ReportCommand<SceneMetadataOptions> scenes({
      .name = "scenes",
      .build = BuildSceneMetadataCommand,
      .run = RunSceneMetadataReport,
    });
    ReportCommand<GeometryMetadataOptions> geometries({
      .name = "geometries",
      .build = BuildGeometryMetadataCommand,
      .run = RunGeometryMetadataReport,
    });
    ReportCommand<ScriptMetadataOptions> scripts({
      .name = "scripts",
      .build = BuildScriptMetadataCommand,
      .run = RunScriptMetadataReport,
    });
    ReportCommand<InventoryReportOptions> inventory({
      .name = "inventory",
      .build = BuildInventoryReportCommand,
      .run = RunInventoryReport,
    });

    const auto commands = std::array<ToolCommand*, 16> {
      &validate,
      &index,
      buffers.get(),
      textures.get(),
      physics_table.get(),
      script_slots.get(),
      script_params.get(),
      input_actions.get(),
      input_mappings.get(),
      physics.get(),
      &dependencies,
      &asset_keys,
      &scenes,
      &geometries,
      &scripts,
      &inventory,
    };

    return RunToolMain(
      ToolInfo {
        .program = "Oxygen.Cooker.Inspector",
        .version = "0.1",
        .about = "Inspect and validate loose cooked content roots "
                 "(filesystem-backed).",
      },
      commands, argc, argv);
  }

} // namespace

} // namespace oxygen::content::inspection

auto main(int argc, char** argv) -> int
{
  return oxygen::content::inspection::RunInspector(argc, argv);
}
