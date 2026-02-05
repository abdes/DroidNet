//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

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
    - Z range = [0, 1]
    - CCW = front face

   Backends (DX12, Vulkan, etc.) must adapt to THIS contract.
  */
  namespace clip {
    inline constexpr float ZNear = 0.0F;
    inline constexpr float ZFar = 1.0F;
    inline constexpr bool FrontFaceCCW = true;
  } // namespace clip

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

} // namespace oxygen
