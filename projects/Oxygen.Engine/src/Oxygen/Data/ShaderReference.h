//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! Shader reference as described in the PAK file resource table.
/*!
 Represents a shader reference used by material assets in the PAK file. This is
 not a first-class asset: it is not named or globally identified, but is
 included inline after a material asset descriptor.

 @see ShaderReferenceDesc, MaterialAssetDesc
*/
class ShaderReference : public oxygen::Object {
  OXYGEN_TYPED(ShaderReference)
public:
  //! Resolves a cooked reference's strings in its material's string table.
  /*!
   @throw std::invalid_argument when a string offset is not in `strings`.
  */
  OXGN_DATA_API ShaderReference(const pak::render::ShaderReferenceDesc& desc,
    const DescriptorStrings& strings);

  ~ShaderReference() override = default;

  OXYGEN_MAKE_NON_COPYABLE(ShaderReference)
  OXYGEN_DEFAULT_MOVABLE(ShaderReference)

  //! Returns the canonical shader source path.
  [[nodiscard]] auto GetSourcePath() const noexcept -> std::string_view
  {
    return source_path_;
  }

  //! Returns the shader entry point.
  [[nodiscard]] auto GetEntryPoint() const noexcept -> std::string_view
  {
    return entry_point_;
  }

  //! Returns the canonical defines string (may be empty).
  [[nodiscard]] auto GetDefines() const noexcept -> std::string_view
  {
    return defines_;
  }

  //! Returns the shader type (aka. the pipeline stage at which the shader
  //! should be used).
  [[nodiscard]] auto GetShaderType() const noexcept -> ShaderType
  {
    return shader_type_;
  }

  //! Returns the shader source hash for validation.
  /*!
   A zero hash indicates that the hash is not set, and cannot be used for
   validation.
  */
  [[nodiscard]] auto GetShaderSourceHash() const noexcept -> uint64_t
  {
    return shader_hash_;
  }

private:
  ShaderType shader_type_ {};
  std::string source_path_;
  std::string entry_point_;
  std::string defines_;
  uint64_t shader_hash_ = 0;
};

} // namespace oxygen::data
