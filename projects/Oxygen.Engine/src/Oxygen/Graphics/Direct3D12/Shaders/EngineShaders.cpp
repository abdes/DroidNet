//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Graphics/Common/ShaderByteCode.h>
#include <Oxygen/Graphics/Common/ShaderManager.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Direct3D12/Shaders/EngineShaders.h>
#include <Oxygen/Graphics/Direct3D12/Shaders/ShaderCompiler.h>

using oxygen::ShaderType;
using oxygen::graphics::ShaderInfo;
using oxygen::graphics::d3d12::EngineShaders;

namespace {
// Specification of engine shaders. Each entry is a ShaderProfile corresponding
// to one of the shaders we want to automatically compile, package and load.
const std::array<ShaderInfo, 7> kEngineShaders = {
  {
    {
      .type = ShaderType::kPixel,
      .relative_path = "Passes/Forward/ForwardMesh.hlsl",
      .entry_point = "PS",
    },
    {
      .type = ShaderType::kVertex,
      .relative_path = "Passes/Forward/ForwardMesh.hlsl",
      .entry_point = "VS",
    },
    {
      .type = ShaderType::kPixel,
      .relative_path = "Passes/Depth/DepthPrePass.hlsl",
      .entry_point = "PS",
    },
    {
      .type = ShaderType::kVertex,
      .relative_path = "Passes/Depth/DepthPrePass.hlsl",
      .entry_point = "VS",
    },
    {
      .type = ShaderType::kCompute,
      .relative_path = "Passes/Lighting/LightCulling.hlsl",
      .entry_point = "CS",
    },
    {
      .type = ShaderType::kVertex,
      .relative_path = "Passes/Ui/ImGui.hlsl",
      .entry_point = "VS",
    },
    {
      .type = ShaderType::kPixel,
      .relative_path = "Passes/Ui/ImGui.hlsl",
      .entry_point = "PS",
    },
  },
};
} // namespace

EngineShaders::EngineShaders(oxygen::PathFinderConfig path_finder_config)
  : path_finder_config_(std::move(path_finder_config))
{
  LOG_SCOPE_F(INFO, "Engine Shaders");

  const auto shared_config
    = std::make_shared<const oxygen::PathFinderConfig>(path_finder_config_);
  const oxygen::PathFinder path_finder(
    shared_config, std::filesystem::current_path());
  const auto workspace_root = path_finder.WorkspaceRoot();

  // Load engine shaders
  compiler_ = std::make_shared<ShaderCompiler>(
    ShaderCompiler::Config { .name = "DXC" });

  ShaderManager::Config shader_manager_config {
    .backend_name = "Direct3D12",
    .archive_dir = workspace_root / "bin/Oxygen",
    .source_dir = workspace_root / "src/Oxygen/Graphics/Direct3D12/Shaders",
    .include_dirs = {
      workspace_root / "src/Oxygen",
    },
    .shaders = std::span(kEngineShaders),
    .compiler = compiler_,
  };
  shaders_ = std::make_unique<ShaderManager>(std::move(shader_manager_config));
}

EngineShaders::~EngineShaders() { LOG_SCOPE_F(INFO, "Engine Shaders cleanup"); }

auto EngineShaders::GetShader(const ShaderRequest& request) const
  -> std::shared_ptr<IShaderByteCode>
{
  return shaders_->GetShaderBytecode(request);
}
