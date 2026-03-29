//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/ActionKey.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Bake.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/BuildPaths.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Catalog.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Execution.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/RequestCompilation.h>

using oxygen::graphics::d3d12::kEngineShaders;
using oxygen::graphics::d3d12::tools::shader_bake::ShaderBakeCommand;
using oxygen::graphics::d3d12::tools::shader_bake::ShaderBakeMode;

namespace oxygen::graphics::d3d12::tools::shader_bake {

namespace {

  auto BuildIncludeDirs(const BakeArgs& args)
    -> std::vector<std::filesystem::path>
  {
    std::vector<std::filesystem::path> include_dirs;
    include_dirs.reserve(2 + args.extra_include_dirs.size());
    include_dirs.push_back(args.oxygen_include_root);
    include_dirs.push_back(args.shader_source_root);
    for (const auto& include_dir : args.extra_include_dirs) {
      include_dirs.push_back(include_dir);
    }
    return include_dirs;
  }

  auto ModeToString(const ShaderBakeMode mode) -> std::string_view
  {
    switch (mode) {
    case ShaderBakeMode::kDev:
      return "dev";
    case ShaderBakeMode::kProduction:
      return "production";
    default:
      return "unknown";
    }
  }

} // namespace

auto BakeShaderLibrary(const BakeArgs& args) -> int
{
  LOG_SCOPE_F(INFO, "ShaderBake");

  const auto layout = GetBuildRootLayout(args.build_root);

  if (args.command == ShaderBakeCommand::kCleanCache) {
    LOG_SCOPE_F(INFO, "ShaderBakeCleanCache");
    LOG_F(INFO, "Cleaning ShaderBake cache under {}",
      ToUtf8PathString(layout.root));
    ClearCache(layout);
    return 0;
  }

  EnsureBuildRootLayout(layout);
  ResetTempDirectory(layout);

  LOG_F(INFO, "command={} mode={} build_root={} out={}",
    args.command == ShaderBakeCommand::kRebuild ? "rebuild" : "update",
    ModeToString(args.mode), ToUtf8PathString(args.build_root),
    ToUtf8PathString(args.out_file));

  const auto expanded_requests = ExpandShaderCatalog(std::span(kEngineShaders));
  LOG_F(INFO, "expanded_requests={}", expanded_requests.size());
  const uint64_t toolchain_hash = ComputeToolchainHash();
  auto include_dirs = BuildIncludeDirs(args);
  const ExecutionOptions options {
    .mode = args.mode,
    .workspace_root = args.workspace_root,
    .shader_source_root = args.shader_source_root,
    .final_archive_path = args.out_file,
    .layout = layout,
    .toolchain_hash = toolchain_hash,
    .include_dirs = include_dirs,
    .requests = expanded_requests,
    .compile_request =
      [&](const ExpandedShaderRequest& request, const size_t index,
        const size_t total_count) {
        return CompileExpandedShaderRequest(
          RequestCompilerConfig {
            .workspace_root = args.workspace_root,
            .shader_source_root = args.shader_source_root,
            .toolchain_hash = toolchain_hash,
            .include_dirs = include_dirs,
          },
          request, index, total_count);
      },
  };

  if (args.command == ShaderBakeCommand::kRebuild) {
    const auto result = ExecuteRebuild(options);
    if (!result.has_value()) {
      return 2;
    }
  } else {
    const auto result = ExecuteUpdate(options);
    if (!result.has_value()) {
      return 2;
    }
  }
  return 0;
}

} // namespace oxygen::graphics::d3d12::tools::shader_bake
