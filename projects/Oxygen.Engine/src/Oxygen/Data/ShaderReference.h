//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! Shader reference as described in the PAK file resource table.
/*!
 Represents a shader reference used by material assets in the PAK file. This is
 not a first-class asset: it is not named or globally identified, but is
 included inline after a material asset descriptor.

 ### Binary Encoding (PAK v1, 216 bytes)

 ```text
 offset size name               description
 ------ ---- ------------------ ---------------------------------------------
 0x00   192  shader_unique_id   Shader unique identifier (null-terminated,
 padded) 0xC0   8    shader_hash        64-bit hash of shader source for
 validation 0xC8   16   reserved           Reserved for future use
 ```

 - `shader_unique_id`: Unique identifier for the shader (e.g.,
   VS@path/to/file.hlsl), maximum size is 192 bytes including null terminator,
   padded with null bytes.
 - `shader_hash`: 64-bit hash of the shader source for validation.

 @see ShaderReferenceDesc, MaterialAssetDesc
*/
class ShaderReference : public oxygen::Object {
  OXYGEN_TYPED(ShaderReference)
public:
  explicit ShaderReference(ShaderType stage, pak::ShaderReferenceDesc desc)
    : stage_(stage)
    , desc_(std::move(desc))
  {
  }

  ~ShaderReference() override = default;

  OXYGEN_MAKE_NON_COPYABLE(ShaderReference)
  OXYGEN_DEFAULT_MOVABLE(ShaderReference)

  //! Returns the shader unique identifier in the format commonly used by the
  //! engine.
  OXGN_DATA_NDAPI auto GetShaderUniqueId() const noexcept -> std::string_view;

  //! Returns the shader type (aka. the pipeline stage at which the shadershould
  //! be used).
  [[nodiscard]] auto GetShaderType() const noexcept -> ShaderType
  {
    return stage_;
  }

  //! Returns the shader source hash for validation.
  /*!
   A zero hash indicates that the hash is not set, and cannot be used for
   validation.
  */
  [[nodiscard]] auto GetShaderSourceHash() const noexcept -> uint64_t
  {
    return desc_.shader_hash;
  }

private:
  ShaderType stage_;
  pak::ShaderReferenceDesc desc_ {};
};

} // namespace oxygen::data
