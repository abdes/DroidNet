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

 ### Binary Encoding (PAK v2, 424 bytes)

 Stores a structured shader request (stage, source path, entry point, defines)
 plus a source hash for validation.

 @see ShaderReferenceDesc, MaterialAssetDesc
*/
class ShaderReference : public oxygen::Object {
  OXYGEN_TYPED(ShaderReference)
public:
  explicit ShaderReference(pak::ShaderReferenceDesc desc)
    : desc_(std::move(desc))
  {
  }

  ~ShaderReference() override = default;

  OXYGEN_MAKE_NON_COPYABLE(ShaderReference)
  OXYGEN_DEFAULT_MOVABLE(ShaderReference)

  //! Returns the canonical shader source path.
  OXGN_DATA_NDAPI auto GetSourcePath() const noexcept -> std::string_view;

  //! Returns the shader entry point.
  OXGN_DATA_NDAPI auto GetEntryPoint() const noexcept -> std::string_view;

  //! Returns the canonical defines string (may be empty).
  OXGN_DATA_NDAPI auto GetDefines() const noexcept -> std::string_view;

  //! Returns the shader type (aka. the pipeline stage at which the shadershould
  //! be used).
  [[nodiscard]] auto GetShaderType() const noexcept -> ShaderType
  {
    return static_cast<ShaderType>(desc_.shader_type);
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
  pak::ShaderReferenceDesc desc_ {};
};

} // namespace oxygen::data
