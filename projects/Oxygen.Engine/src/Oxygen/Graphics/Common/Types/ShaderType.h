//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

//! Enum representing the different types of shaders supported by Direct3D 12.
enum class ShaderType : uint8_t {
    kUnknown = 0, //!< Unknown Shader Type: Used for error handling or uninitialized state.

    kVertex, //!< Vertex Shader: Processes each vertex and transforms vertex positions.
    kPixel, //!< Pixel Shader: Processes each pixel and determines the final color.
    kGeometry, //!< Geometry Shader: Processes entire primitives and can generate additional geometry.
    kHull, //!< Hull Shader: Used in tessellation, processes control points.
    kDomain, //!< Domain Shader: Used in tessellation, processes tessellated vertices.
    kCompute, //!< Compute Shader: Used for general-purpose computing tasks on the GPU.
    kAmplification, //!< Amplification Shader: Part of the mesh shader pipeline, processes groups of vertices.
    kMesh, //!< Mesh Shader: Part of the mesh shader pipeline, processes meshlets.

    kCount //!< Count of shader types.
};

//! String representation of enum values in `ShaderType`.
OXYGEN_GFX_API auto to_string(ShaderType value) -> const char*;

} // namespace graphics
