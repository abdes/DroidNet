//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include "Oxygen/Graphics/Common/api_export.h"

namespace oxygen::graphics {

//! Enum representing the different types of shaders supported by Direct3D 12.
enum class ShaderType : uint8_t {
    kVertex = 0, //!< Vertex Shader: Processes each vertex and transforms vertex positions.
    kPixel = 1, //!< Pixel Shader: Processes each pixel and determines the final color.
    kGeometry = 2, //!< Geometry Shader: Processes entire primitives and can generate additional geometry.
    kHull = 3, //!< Hull Shader: Used in tessellation, processes control points.
    kDomain = 4, //!< Domain Shader: Used in tessellation, processes tessellated vertices.
    kCompute = 5, //!< Compute Shader: Used for general-purpose computing tasks on the GPU.
    kAmplification = 6, //!< Amplification Shader: Part of the mesh shader pipeline, processes groups of vertices.
    kMesh = 7, //!< Mesh Shader: Part of the mesh shader pipeline, processes meshlets.

    kCount = 8 //!< Count of shader types.
};

//! String representation of enum values in `ShaderType`.
OXYGEN_GFX_API auto to_string(ShaderType value) -> const char*;

} // namespace graphics
