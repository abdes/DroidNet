//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

//! GPU resource access modes.
enum class ResourceAccessMode : uint8_t {
    //! Invalid access mode.
    kInvalid,

    //! GPU read-only resource, for example a material's texture.
    /*!
      Content cannot be accessed by the CPU. Can be written to only once.
      This is the preferred access mode, as it has the lowest overhead.
    */
    kImmutable,

    //! GPU read-write resource, for example a texture used as a render target
    //! or a static texture sampled in a shader.
    /*!
      Content cannot be accessed by the CPU. Can be written to many times per frame.
    */
    kGpuOnly,

    //! GPU read-only resource, for example a constant buffer.
    /*!
      The content can be written by the CPU.
      \warning Memory accesses must be properly synchronized as it's not double-buffered.
    */
    kUpload,

    //! GPU read-only resource, frequently written by CPU.
    /*!
      The content can be written by the CPU. Assumes the data will be written to
      every frame. This mode uses no actual Resource/Buffer allocation. Instead,
      an internal Ring Buffer is used to write data.
    */
    kVolatile,

    //! Read-back resource, for example a screenshot texture.
    /*!
      The content can't be accessed directly by the GPU (only via Copy operations).
      The data can be read by the CPU.
      \warning Memory accesses must be properly synchronized as it's not double-buffered.
    */
    kReadBack
};

//! String representation of enum values in `ResourceAccessMode`.
OXYGEN_GFX_API auto to_string(ResourceAccessMode value) -> const char*;

} // namespace oxygen::graphics
