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
const std::array<ShaderInfo, 5> kEngineShaders = {
  {
    {
      .type = ShaderType::kPixel,
      .relative_path = "FullScreenTriangle.hlsl",
      .entry_point = "PS",
    },
    {
      .type = ShaderType::kVertex,
      .relative_path = "FullScreenTriangle.hlsl",
      .entry_point = "VS",
    },
    {
      .type = ShaderType::kPixel,
      .relative_path = "DepthPrePass.hlsl",
      .entry_point = "PS",
    },
    {
      .type = ShaderType::kVertex,
      .relative_path = "DepthPrePass.hlsl",
      .entry_point = "VS",
    },
    {
      .type = ShaderType::kCompute,
      .relative_path = "LightCulling.hlsl",
      .entry_point = "CS",
    },
  },
};
} // namespace

EngineShaders::EngineShaders()
{
  LOG_SCOPE_F(INFO, "Engine Shaders");

  // Load engine shaders
  compiler_ = std::make_shared<ShaderCompiler>(
    ShaderCompiler::Config { .name = "DXC" });
  // TODO: Make this better by not hard-coding the path
  ShaderManager::Config shader_manager_config {
    .backend_name = "Direct3D12",
    .archive_dir = R"(bin\Oxygen)",
    .source_dir = R"(src\Oxygen\Graphics\Direct3D12\Shaders)",
    .shaders = std::span(kEngineShaders),
    .compiler = compiler_,
  };
  shaders_ = std::make_unique<ShaderManager>(std::move(shader_manager_config));
}

EngineShaders::~EngineShaders() { LOG_SCOPE_F(INFO, "Engine Shaders cleanup"); }

auto EngineShaders::GetShader(std::string_view unique_id) const
  -> std::shared_ptr<IShaderByteCode>
{
  return shaders_->GetShaderBytecode(unique_id);
}
