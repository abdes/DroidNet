//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Data/ShaderType.h>

namespace oxygen::data {

//! A shader asset as stored in the PAK file.
/*!
 ### Binary Encoding:
 ```text
 offset size name          description
 ------ ---- ------------- -----------------------------------------------------
 0x00   4    shader_type   Shader type maps to `ShaderType` enum
 0x04   4    name_length   Length of shader name in bytes (excluding this field)
 0x08   56   shader_name   Shader name data, not null-terminated
 ```

 @note Packed to 64 bytes total. Not aligned.
 @note (Shader type, Shader Name) produce an engine wide unique ID for the
 shader. Shaders are compiled/recompiled if needed at engine startup, and their
 binary blobs are stored separately from the PAK file, and loaded separately by
 the `ShaderManager`.

 @see ShaderType
*/
class ShaderAsset : public oxygen::Object {
  OXYGEN_TYPED(ShaderAsset)

public:
  ShaderAsset() = default;
  ShaderAsset(ShaderType type, std::string name)
    : shader_type(type)
    , shader_name(std::move(name))
  {
  }
  ~ShaderAsset() override = default;

  OXYGEN_MAKE_NON_COPYABLE(ShaderAsset)
  OXYGEN_DEFAULT_MOVABLE(ShaderAsset)

  [[nodiscard]] auto GetShaderType() const noexcept -> ShaderType
  {
    return shader_type;
  }

  [[nodiscard]] auto GetShaderName() const noexcept -> std::string_view
  {
    return shader_name;
  }

private:
  data::ShaderType shader_type { data::ShaderType::kUnknown };
  std::string shader_name;
};

} // namespace oxygen::data
