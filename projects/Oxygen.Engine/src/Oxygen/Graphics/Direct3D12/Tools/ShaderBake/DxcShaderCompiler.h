//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <wrl/client.h>

#include <Oxygen/Graphics/Common/ShaderByteCode.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/FileFingerprint.h>

struct IDxcCompiler3;
struct IDxcUtils;

namespace oxygen::graphics::d3d12::tools::shader_bake {

class DxcShaderCompiler {
public:
  struct CompileOptions {
    std::filesystem::path workspace_root;
    std::vector<std::filesystem::path> include_dirs;
    std::vector<ShaderDefine> defines {};
    std::string object_output_name;
    std::string debug_output_name;
  };

  struct CompileResult {
    std::unique_ptr<IShaderByteCode> bytecode;
    std::vector<std::byte> pdb;
    std::vector<DependencyFingerprint> dependencies;
    std::string diagnostics;

    [[nodiscard]] auto Succeeded() const noexcept -> bool
    {
      return bytecode != nullptr;
    }
  };

  struct Config {
    std::string name { "DXC" };
    std::map<std::wstring, std::wstring> global_defines;
  };

  explicit DxcShaderCompiler(Config config);
  ~DxcShaderCompiler();

  DxcShaderCompiler(const DxcShaderCompiler&) = delete;
  auto operator=(const DxcShaderCompiler&) -> DxcShaderCompiler& = delete;
  DxcShaderCompiler(DxcShaderCompiler&&) noexcept = default;
  auto operator=(DxcShaderCompiler&&) noexcept -> DxcShaderCompiler& = default;

  [[nodiscard]] auto CompileFromSource(const std::u8string& shader_source,
    const ShaderInfo& shader_info, const CompileOptions& options) const
    -> CompileResult;

  [[nodiscard]] auto GetConfig() const noexcept -> const Config&
  {
    return config_;
  }

private:
  Config config_ {};

  Microsoft::WRL::ComPtr<IDxcCompiler3> compiler_;
  Microsoft::WRL::ComPtr<IDxcUtils> utils_;
};

} // namespace oxygen::graphics::d3d12::tools::shader_bake
