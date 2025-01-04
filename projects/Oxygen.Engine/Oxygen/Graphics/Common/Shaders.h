//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Graphics/Common/Types/ShaderType.h"
#include "Oxygen/api_export.h"

namespace oxygen::graphics {
struct CompiledShaderInfo;
} // namespace oxygen::graphics

namespace oxygen::graphics {

// note: the shader name is the file name component of the path including the extension
struct ShaderProfile {
  ShaderType type; //< Shader type.
  std::string path; //< Path to the shader source file, relative to the engine shaders directory.
  std::string entry_point { "main" }; //< Entry point function name.
};

OXYGEN_API [[nodiscard]]
auto MakeShaderIdentifier(ShaderType shader_type, const std::string& relative_path) -> std::string;
OXYGEN_API [[nodiscard]]
auto MakeShaderIdentifier(const ShaderProfile& shader) -> std::string;

} // namespace oxygen::graphics
