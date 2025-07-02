//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <type_traits>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics::detail {

//! Represents the kind of data stored in a format.
enum class FormatKind : uint8_t {
  kInteger,
  kNormalized,
  kFloat,
  kDepthStencil
};

//! Contains information about a specific graphics format.
struct FormatInfo {
  Format format; //!< The format identifier.
  uint8_t bytes_per_block; //!< Number of bytes in a format block.
  uint8_t block_size; //!< Size of a block in the format.
  FormatKind kind; //!< Kind of data stored in the format.
  bool has_red : 1; //!< Whether the format contains a red channel.
  bool has_green : 1; //!< Whether the format contains a green channel.
  bool has_blue : 1; //!< Whether the format contains a blue channel.
  bool has_alpha : 1; //!< Whether the format contains an alpha channel.
  bool has_depth : 1; //!< Whether the format contains depth data.
  bool has_stencil : 1; //!< Whether the format contains stencil data.
  bool is_signed : 1; //!< Whether the format uses signed values.
  bool is_srgb : 1; //!< Whether the format uses sRGB color space.
};

//! Retrieves detailed information about a specific format.
/*!
 \param format The format to retrieve information for.
 \return A reference to the format information structure.
*/
OXYGEN_GFX_API auto GetFormatInfo(Format format) -> const FormatInfo&;

//! Flags indicating what operations are supported by a format.
enum class FormatSupport : uint32_t { // NOLINT(*-enum-size)
  kNone = 0,

  kBuffer = OXYGEN_FLAG(0), //!< Can be used in a buffer.
  kIndexBuffer = OXYGEN_FLAG(1), //!< Can be used in an index buffer.
  kVertexBuffer = OXYGEN_FLAG(2), //!< Can be used in a vertex buffer.

  kTexture = OXYGEN_FLAG(3), //!< Can be used in a texture.
  kDepthStencil = OXYGEN_FLAG(4), //!< Can be used as depth/stencil format.
  kRenderTarget = OXYGEN_FLAG(5), //!< Can be used as render target.
  kBlendable = OXYGEN_FLAG(6), //!< Can be used with blending.

  kShaderLoad = OXYGEN_FLAG(7), //!< Can be loaded in a shader.
  kShaderSample = OXYGEN_FLAG(8), //!< Can be sampled in a shader.
  kShaderUavLoad = OXYGEN_FLAG(9), //!< Can be loaded as UAV in a shader.
  kShaderUavStore = OXYGEN_FLAG(10), //!< Can be stored as UAV in a shader.
  kShaderAtomic = OXYGEN_FLAG(11), //!< Can be used with atomic operations.
};

OXYGEN_DEFINE_FLAGS_OPERATORS(FormatSupport)

} // namespace oxygen::graphics::detail
