//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace oxygen {

// ------------------------------------------------------------------------
// Type aliases (the ONLY math types exposed by this header)
// ------------------------------------------------------------------------

using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using Mat3 = glm::mat3;
using Mat4 = glm::mat4;
using Quat = glm::quat;

//! Oxygen engine coordinate-space conventions and related constants.
/*!
 Oxygen engine is:
   - RIGHT-HANDED
   - Z-UP
   - FORWARD = -Y
   - RIGHT   = +X

 These conventions are ENGINE LAW. They are not configurable.
 Every system (math, rendering, physics, animation, editor, importers)
 must obey this contract without exception.
*/
namespace space {

  //! Direction vectors for movement in WORLD space.
  namespace move {
    inline constexpr Vec3 Right { +1.0F, 0.0F, 0.0F }; //! Right along +X
    inline constexpr Vec3 Left { -1.0F, 0.0F, 0.0F }; //! Left along -X

    inline constexpr Vec3 Forward { 0.0F, -1.0F, 0.0F }; //! Forward along -Y
    inline constexpr Vec3 Back { 0.0F, +1.0F, 0.0F }; //! Back along +Y

    inline constexpr Vec3 Up { 0.0F, 0.0F, +1.0F }; //! Up along +Z
    inline constexpr Vec3 Down { 0.0F, 0.0F, -1.0F }; //! Down along -Z
  } // namespace move

  //! Direction vectors for looking in VIEW space. The other directions are
  //! simple negations.
  //!
  //! View space is camera-local and uses the standard graphics convention
  //! forward = -Z. World space remains Z-up with forward = -Y. The view
  //! matrix is responsible for mapping world-space axes into this camera-
  //! local basis, which keeps projection and clip-space math consistent.
  namespace look {
    inline constexpr Vec3 Forward { 0.0F, 0.0F, -1.0F }; //! Forward along -Z
    inline constexpr Vec3 Right { +1.0F, 0.0F, 0.0F }; //! Right along +X
    inline constexpr Vec3 Up { 0.0F, +1.0F, 0.0F }; //! Up along +Y
  } // namespace look

  //! Clip-space conventions and related constants.
  /*!
   Unified clip-space contract:

    - Right-handed
    - Forward = -Z
    - Reversed-Z range = [1, 0] where near = 1 and far = 0
    - CCW = front face

   Backends (DX12, Vulkan, etc.) must adapt to THIS contract.
  */
  namespace clip {
    inline constexpr float ZNear = 1.0F;
    inline constexpr float ZFar = 0.0F;
    inline constexpr bool FrontFaceCCW = true;
  } // namespace clip

  //! Texture-space conventions: how UVs address an image and how a tangent
  //! frame carries a tangent-space normal map onto a surface.
  /*!
   These conventions follow Direct3D, the engine's main backend. Vulkan and
   Metal address textures the same way; the normal map convention is a content
   convention that no API or GPU imposes. Like the space conventions above,
   they are ENGINE LAW: importers, the tangent generator, procedural meshes and
   shaders obey them, and every source is converted to them when cooked.

   ### Images and UVs

   - A decoded image holds its rows top first: row 0 is the image's top.
     Decoders flip sources stored bottom-up (KTX2 with `KTXorientation` "u").
   - UV (0, 0) is the top-left corner of the image: U grows to the right (+U)
     and V grows DOWN (+V). UV (1, 1) is the bottom-right corner.
   - glTF UVs already follow this. FBX UVs grow up from the bottom row and are
     converted on import: v' = 1 - v.

   ### Tangent-space normal maps

   - A texel encodes a unit vector, each channel mapping [0, 1] to [-1, 1]:
     +X (red) points right in the image, +Y (green) DOWN the image ("green
     down", the Direct3D convention) and +Z (blue) out of the surface.
   - Maps authored green-up ("OpenGL") have their green channel inverted when
     cooked. Who decides a map's convention:
     - glTF: its specification; every glTF normal map is green-up.
     - FBX: the application that authored the file, which FBX records. 3ds Max
       writes green-down maps; Maya, MotionBuilder, Blender, Houdini and
       Cinema 4D write green-up ones. Maps from any other application are
       taken green-up and the import reports
   `fbx.normal_map_convention_assumed`.
     - A standalone image records nothing and is taken green-down.
     - The import setting `flip_normal_green`, for the import or for one
       texture, reverses that decision for a map authored the other way.

   ### Tangent frames

   - Each vertex carries a normal N, a tangent T and a bitangent B. T points
     along ImageRight (+dP/dU, where U grows over the surface) and B along
     ImageDown (+dP/dV), so a normal map's X and Y land on T and B.
   - B = cross(N, T) * w, where the handedness w is +1, or -1 where the UV
     layout is mirrored. Shaders evaluate a normal map as
     `T * n.x + B * n.y + N * n.z`, taking w from the vertex bitangent.
   - Generated frames (missing tangents) follow +dP/dU and +dP/dV.
   - glTF's bitangent, cross(N, T) * TANGENT.w, points up the image and is
     negated on import. An FBX's authored bitangent points up its V-up UVs and
     is negated with the V flip.
   - Procedural meshes follow the same rules (abdes/DroidNet#112).
  */
  namespace texture {
    //! Image right in UV space: +U, the direction of a vertex's tangent.
    inline constexpr Vec2 ImageRight { +1.0F, 0.0F };
    //! Image down in UV space: +V, the direction of a vertex's bitangent.
    inline constexpr Vec2 ImageDown { 0.0F, +1.0F };
  } // namespace texture

} // namespace space

//! Physics-related constants. Use the same world space as the engine. Gravity
//! is always along -Z.
namespace physics {
  inline constexpr float GravityMagnitude = 9.80665F;
  inline constexpr Vec3 Gravity { 0.0F, 0.0F, -GravityMagnitude };
} // namespace physics

//! Mathematical constants and utilities.
namespace math {
  inline constexpr float Pi = glm::pi<float>();
  inline constexpr float TwoPi = glm::two_pi<float>();
  inline constexpr float HalfPi = glm::half_pi<float>();
  inline constexpr float DegToRad = glm::pi<float>() / 180.0F;
  inline constexpr float RadToDeg = 180.0F / glm::pi<float>();

  inline constexpr float Epsilon = 1e-6F;
  inline constexpr float EpsilonPosition = 1e-4F;
  inline constexpr float EpsilonDirection = 1e-4F;
  inline constexpr float EpsilonQuaternion = 1e-5F;
} // namespace math

namespace packing {

  inline constexpr uint32_t kRootConstantsMaxSize = 256;
  inline constexpr uint32_t kStructuredBufferAlignment = 16;
  inline constexpr uint32_t kShaderDataFieldAlignment = 16;
  inline constexpr uint32_t kConstantBufferAlignment = 256;

} // namespace packing

} // namespace oxygen
