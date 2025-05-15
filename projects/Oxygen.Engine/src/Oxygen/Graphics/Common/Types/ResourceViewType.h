//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

//! The different types of bindings for resources.
enum class ResourceViewType : uint8_t {
    kNone, //!< No binding

    //! {@
    //! Types used in bindless rendering descriptor tables.

    kTexture_SRV, //!< Shader Resource View for textures
    kTexture_UAV, //!< Unordered Access View for textures

    kTypedBuffer_SRV, //!< Shader Resource View for typed buffers
    kTypedBuffer_UAV, //!< Unordered Access View for typed buffers
    kStructuredBuffer_UAV, //!< Unordered Access View for structured buffers
    kStructuredBuffer_SRV, //!< Shader Resource View for structured buffers
    kRawBuffer_SRV, //!< Shader Resource View for raw buffers
    kRawBuffer_UAV, //!< Unordered Access View for raw buffers

    kConstantBuffer, //!< Constant buffer

    kSampler, //!< Sampler
    kSamplerFeedbackTexture_UAV, //!< Sampler feedback texture UAV

    kRayTracingAccelStructure, //!< Ray tracing acceleration structure

    //! @}

    kTexture_DSV, //!< Depth Stencil View for textures
    kTexture_RTV, //!< Render Target View for textures

    // Push constants are a special case that exists parallel to both
    // traditional and bindless binding systems. They're directly passed to the
    // shader through root constants (DX12) or push constants (Vulkan).

    kMax //!< Maximum value sentinel
};

//! String representation of enum values in `ResourceViewType`.
OXYGEN_GFX_API auto to_string(ResourceViewType value) -> const char*;

} // namespace oxygen::graphics
