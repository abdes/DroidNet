//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <utility>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Tools/Common/ToolApp.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/BatchCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/CliBuilder.h>
#include <Oxygen/Cooker/Tools/ImportTool/EngineContentCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/FbxCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/GlobalOptions.h>
#include <Oxygen/Cooker/Tools/ImportTool/GltfCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/ImportCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/ImportSession.h>
#include <Oxygen/Cooker/Tools/ImportTool/InputCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/PhysicsSidecarCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/ReclaimCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/RemoveCookedCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/ScriptCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/ScriptingSidecarCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/SourceAnalysisCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/SourceInspectionCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/StopSignal.h>
#include <Oxygen/Cooker/Tools/ImportTool/TextureCommand.h>
#include <Oxygen/Scripting/Analysis/LuauCompiler.h>

#ifndef OXYGEN_IMPORT_TOOL_VERSION
#  error OXYGEN_IMPORT_TOOL_VERSION must be defined for ImportTool CLI version.
#endif

namespace oxygen::content::import::tool {

namespace {

  auto CompileScriptWithLuau(
    const AsyncImportService::ScriptCompileRequest& request)
    -> AsyncImportService::ScriptCompileResult
  {
    auto output = oxygen::scripting::CompileLuau({
      .source_name = request.source_name,
      .source = request.source_bytes,
      .mode = request.compile_mode,
    });
    const auto success = output.Succeeded();
    return AsyncImportService::ScriptCompileResult {
      .success = success,
      .bytecode = std::move(output.bytecode),
      .diagnostics = std::move(output.diagnostics),
    };
  }

  auto RunImportTool(int argc, char** argv) -> int
  {
    GlobalOptions global_options;
    BatchCommand batch_command(&global_options);
    EngineContentCommand engine_content_command;
    SourceInspectionCommand source_inspection_command;
    SourceAnalysisCommand source_analysis_command(&global_options);
    FbxCommand fbx_command(&global_options);
    GltfCommand gltf_command(&global_options);
    InputCommand input_command(&global_options);
    PhysicsSidecarCommand physics_sidecar_command(&global_options);
    ReclaimCommand reclaim_command;
    RemoveCookedCommand remove_cooked_command;
    ScriptCommand script_command(&global_options);
    ScriptingSidecarCommand scripting_sidecar_command(&global_options);
    TextureCommand texture_command(&global_options);
    const auto import_commands = std::array<ImportCommand*, 13> {
      &texture_command,
      &fbx_command,
      &gltf_command,
      &input_command,
      &script_command,
      &scripting_sidecar_command,
      &physics_sidecar_command,
      &batch_command,
      &engine_content_command,
      &source_inspection_command,
      &source_analysis_command,
      &reclaim_command,
      &remove_cooked_command,
    };
    auto commands
      = std::array<toolkit::ToolCommand*, import_commands.size()> {};
    std::ranges::copy(import_commands, commands.begin());

    AsyncImportService::Config service_config {};
    service_config.script_compile_callback = CompileScriptWithLuau;
    ImportSession session(
      global_options, batch_command, service_config, import_commands);
    const auto global_option_groups = BuildGlobalOptionGroups(global_options);

    return toolkit::RunToolMain(
      toolkit::ToolInfo {
        .program = "Oxygen.Cooker.ImportTool",
        .version = OXYGEN_IMPORT_TOOL_VERSION,
        .about = "Invoke async import jobs for standalone assets",
        .cooks_content = true,
        .global_options = global_option_groups,
        .lifecycle = observer_ptr<toolkit::ToolLifecycle> { &session },
      },
      commands, argc, argv);
  }

} // namespace

} // namespace oxygen::content::import::tool

auto main(int argc, char** argv) -> int
{
  oxygen::content::import::tool::InstallStopHandlers();
  return oxygen::content::import::tool::RunImportTool(argc, argv);
}
