//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12shader.h>
#include <dxcapi.h>
#include <vector>
#include <wrl/client.h>

#include "Oxygen/base/macros.h"
#include "Oxygen/Base/Types.h"
#include "Oxygen/Renderers/Direct3d12/Shaders.h"

namespace oxygen {

  class ShaderCompiler
  {
  public:
    ShaderCompiler() = default;
    ~ShaderCompiler() = default;

    OXYGEN_MAKE_NON_COPYABLE(ShaderCompiler);
    OXYGEN_MAKE_NON_MOVEABLE(ShaderCompiler);

    bool Init();

    bool Compile(
      const char* source,
      uint32_t source_size,
      const char* source_name,
      renderer::d3d12::ShaderType type,
      const std::vector<DxcDefine>& defines,
      Microsoft::WRL::ComPtr<IDxcBlob>& output) const;

    bool Disassemble(
      const Microsoft::WRL::ComPtr<IDxcBlob>& bytecode,
      Microsoft::WRL::ComPtr<IDxcBlob>& disassembly) const;

    bool Reflect(
      const Microsoft::WRL::ComPtr<IDxcBlob>& bytecode,
      Microsoft::WRL::ComPtr<ID3D12ShaderReflection>& reflection);

  private:
    // Compiler and Utils
    Microsoft::WRL::ComPtr<IDxcCompiler3> compiler_;
    Microsoft::WRL::ComPtr<IDxcUtils> utils_;

    // Default include handler
    Microsoft::WRL::ComPtr<IDxcIncludeHandler> include_processor_;
  };
} // namespace oxygen
