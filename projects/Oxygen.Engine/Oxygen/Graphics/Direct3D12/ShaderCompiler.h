//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Graphics/Common/ShaderCompiler.h"

#include <d3d12shader.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include "Oxygen/Graphics/Direct3d12/api_export.h"

namespace oxygen::renderer::d3d12 {

class ShaderCompiler : public renderer::ShaderCompiler
{
  using Base = renderer::ShaderCompiler;

 public:
  template <typename... Args>
  explicit ShaderCompiler(Args&&... args)
    : Base("DXC Shader Compiler", std::forward<Args>(args)...)
  {
  }

  ~ShaderCompiler() override = default;

  OXYGEN_MAKE_NON_COPYABLE(ShaderCompiler);
  OXYGEN_DEFAULT_MOVABLE(ShaderCompiler);

  [[nodiscard]] auto CompileFromSource(
    const std::u8string& shader_source,
    const ShaderProfile& shader_profile) const -> std::unique_ptr<IShaderByteCode> override;

 protected:
  OXYGEN_D3D12_API void OnInitialize() override;

 private:
  // Compiler and Utils
  Microsoft::WRL::ComPtr<IDxcCompiler3> compiler_;
  Microsoft::WRL::ComPtr<IDxcUtils> utils_;

  // Default include handler
  Microsoft::WRL::ComPtr<IDxcIncludeHandler> include_processor_;
};
} // namespace oxygen
