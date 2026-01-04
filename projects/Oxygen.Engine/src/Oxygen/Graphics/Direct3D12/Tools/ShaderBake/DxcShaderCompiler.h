//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <wrl/client.h>

#include <Oxygen/Graphics/Common/ShaderByteCode.h>
#include <Oxygen/Graphics/Common/Shaders.h>

struct IDxcCompiler3;
struct IDxcUtils;
struct IDxcIncludeHandler;

namespace oxygen::graphics::d3d12::tools::shader_bake {

class DxcShaderCompiler {
public:
  struct CompileOptions {
    std::vector<std::filesystem::path> include_dirs;
    std::vector<ShaderDefine> defines {};
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
    -> std::unique_ptr<IShaderByteCode>;

  [[nodiscard]] auto GetConfig() const noexcept -> const Config&
  {
    return config_;
  }

private:
  Config config_ {};

  Microsoft::WRL::ComPtr<IDxcCompiler3> compiler_;
  Microsoft::WRL::ComPtr<IDxcUtils> utils_;
  Microsoft::WRL::ComPtr<IDxcIncludeHandler> include_processor_;
};

} // namespace oxygen::graphics::d3d12::tools::shader_bake
