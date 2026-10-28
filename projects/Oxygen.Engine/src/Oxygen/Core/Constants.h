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
   - FORWARD = -Y: the direction an asset's front and every node face
   - +X      : `move::Right`, the world's right for a viewer looking along +Y
               at an asset's front. It is NOT a frame right: a frame facing
               FORWARD (-Y) with +Z up has its right on -X (`node::Right`).
               Never pair `move::Right` with `Forward` and `Up` to build a
               rotation.

 Every node, cameras included, uses the `node` frame below.

 These conventions are ENGINE LAW. They are not configurable.
 Every system (math, rendering, physics, animation, editor, importers)
 must obey this contract without exception.

 ### Units

 Code works in metres, seconds and radians. Degrees exist only in authored
 data and UI, and each crossing converts once:
 - Node rotations: the editor and the scene schema's
   `/local_rotation_euler_degrees` (X-Y-Z Euler angles, below).
 - Camera field of view: the schema's `/field_of_view_degrees`
   (`degrees-to-radians`); spot cone angles show in degrees but are stored in
   radians (`display_unit: degree`).
 - Atmosphere `transmittance_min_light_elevation_deg` and the import setting
   `normals_smoothing_angle_deg`, which stay degrees to their consumer.
 - FBX field of view and full spot cone angles, converted on import.
 - DemoShell UI fields and Lua scripts, which call `math.rad`.

 ### Rotations

 - Every rotation follows the right-hand rule: a positive angle about an axis
   turns counter-clockwise seen from the axis tip looking back to the origin.
 - Quaternions: `Quat` (glm) stores x, y, z, w in memory, and its constructor
   takes (w, x, y, z). Every serialized quaternion is [x, y, z, w]: cooked
   `NodeRecord::rotation`, scene descriptor `rotation`, override `as_quat`.
   Identity is [0, 0, 0, 1].
 - Euler angles: one order, X-Y-Z. Angles (x, y, z) turn about X, then Y,
   then Z, each about a parent axis, so R = Rz * Ry * Rx: glm's
   `quat(vec3)` / `eulerAngles`,
 `data::pak::world::EulerXyzDegreesToQuaternion`, the editor's
 `TransformConverter` and Lua's `quat_from_euler_xyz`. The node frame is
 Blender's (Z up, front -Y), so its XYZ angles give the same orientation; yaw
 about world +Z applies last, and gimbal lock is at ±90° about Y, the forward
 (roll) axis. Read back, the Y angle lies in [-90°, 90°].

 ### Matrices

 - glm matrices are column-major and transform column vectors: `M * v`. A
   node's local matrix is T * R * S and its world matrix parent * local.
 - Shaders receive matrices unchanged and multiply `mul(M, v)`; DXC compiles
   with explicit column-major packing (`-Zpc`). A matrix built in a shader from
   vectors (`float3x3(a, b, c)`) has them as rows, so `mul(M, v)` projects v
   onto them.
 - Normals use the inverse transpose of the world matrix's upper 3x3.

 ### Winding and mirroring

 - A triangle (a, b, c) is front-facing on the side cross(b - a, c - a)
   points to: its vertices run counter-clockwise for a viewer on that side
   (`clip::FrontFaceCCW`), and back faces are culled. Stored vertex normals
   point to the front side; a closed mesh's front is its outside.
 - A node world matrix with a negative determinant mirrors its mesh; the
   renderer, not the importer, restores the winding by flipping the front face
   of that draw. A mirroring transform an importer bakes into vertices swaps
   each triangle's last two indices and transforms tangents and bitangents
   with it. Shaders take the tangent frame's handedness from the transformed
   bitangent (the sign of dot(cross(N, T), B)), so mirrored frames stay valid.

 ### Source conventions

 - glTF (+Y up, asset front +Z) converts as (x, y, z) -> (x, -z, y); glTF
   cameras and lights face local -Z and get a half turn about +Z into the
   `node` frame.
 - FBX is converted by ufbx to Z up and -Y forward; a left-handed file is
   mirrored along Y.

 ### Screen and viewport

 - Pixel (0, 0) is the top-left corner of the viewport, with x right and y
   down; viewports and scissors are top-left based.
 - NDC is +X right, +Y up and depth [0, 1] reversed (`clip`), on every
   backend: projections never flip y. Screen UV = (0.5 + 0.5 x, 0.5 - 0.5 y)
   from NDC (x, y); depth reconstruction, HZB occlusion and shadow paths all
   flip y this way, and picking reads draw IDs at top-left pixel coordinates.
*/
namespace space {

  //! Direction vectors for movement in WORLD space.
  /*!
   These are world axes, not a viewer frame. `Forward` (-Y) is the direction
   an unrotated node faces; `Right` (+X) is the world's right as seen by a
   viewer looking along +Y at that node's front. A frame facing `Forward`
   with `Up` has `Left` on its right (`node::Right`), so never build a
   rotation from `Forward`, `Up` and `Right` together.
  */
  namespace move {
    inline constexpr Vec3 Right { +1.0F, 0.0F, 0.0F }; //! Right along +X
    inline constexpr Vec3 Left { -1.0F, 0.0F, 0.0F }; //! Left along -X

    inline constexpr Vec3 Forward { 0.0F, -1.0F, 0.0F }; //! Forward along -Y
    inline constexpr Vec3 Back { 0.0F, +1.0F, 0.0F }; //! Back along +Y

    inline constexpr Vec3 Up { 0.0F, 0.0F, +1.0F }; //! Up along +Z
    inline constexpr Vec3 Down { 0.0F, 0.0F, -1.0F }; //! Down along -Z
  } // namespace move

  //! The local frame of every scene node, cameras and lights included.
  /*!
   A node faces its local forward (-Y) with its local up (+Z). Its right is
   forward x up = -X: the frame is right-handed, so a node facing -Y has +X on
   its left. `move::Right` (+X) is the world's right as seen by a viewer who
   faces an unrotated node, looking along +Y at its front.

   A camera looks along its node's forward with its node's up at the top of
   the image; a directional or spot light shines along its node's forward.
   `transforms::LookRotation` builds a node rotation from a direction and
   `transforms::NodeViewMatrix` a camera's view matrix.
  */
  namespace node {
    inline constexpr Vec3 Forward = move::Forward; //! Forward along -Y
    inline constexpr Vec3 Up = move::Up; //! Up along +Z
    inline constexpr Vec3 Right { -1.0F, 0.0F, 0.0F }; //! Right along -X
  } // namespace node

  //! Direction vectors for looking in VIEW space. The other directions are
  //! simple negations.
  //!
  //! View space is the space a view matrix maps world positions into, with
  //! the standard graphics convention forward = -Z, up = +Y, right = +X. It
  //! describes no node: a camera node uses the `node` frame, and its view
  //! matrix maps the node's forward, up and right onto these axes.
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
   - B = -cross(N, T) * w, where the handedness w is +1 for an unmirrored
     layout and -1 for a mirrored one (glTF's TANGENT.w). A layout is
     unmirrored when dot(cross(N, T), B) < 0: seen from the side N points to,
     the image reads with +U to the viewer's right and +V down, whichever way
     the viewer is turned about N. B is authoritative: it is +dP/dV and w is
     derived from it, never the reverse. Shaders rebuild
     B = cross(N, T) * sign(dot(cross(N, T), B_vertex)) after transforming and
     re-orthogonalizing, and evaluate `T * n.x + B * n.y + N * n.z`.
   - A vertex without a usable bitangent takes B = -cross(N, T) (w = +1).
   - Generated frames (missing tangents) follow +dP/dU and +dP/dV.
   - glTF's bitangent, cross(N, T) * TANGENT.w, points up the image and is
     negated on import. An FBX's authored bitangent points up its V-up UVs and
     is negated with the V flip.
   - Procedural meshes follow the same rules.

   ### UV layout of generated surfaces

   - A generated surface is textured upright for a viewer outside it who
     faces its front (looks along +Y with +Z up) and turns to see each face.
   - Vertical face with outward normal N (N.z = 0): T = cross(Up, N),
     B = Down. Front (-Y): T = +X; right (+X): T = +Y; back (+Y): T = -X;
     left (-X): T = -Y.
   - Top face (N = +Z): T = +X, B = Forward (-Y). Bottom face (N = -Z):
     T = +X, B = Back (+Y).
   - Surfaces of revolution about +Z (sphere, capsule, cylinder, cone, torus
     main ring): the seam is at +Y (u = 0 and 1); u = 0.5 at -Y, 0.75 at +X,
     0.25 at -X (u grows counter-clockwise seen from +Z, which is +X at the
     front). v = 0 at the +Z end and v = 1 at the -Z end, linear along the
     meridian. Caps follow the top/bottom face rule.
   - A torus's v runs around its tube: v = 0 and 1 on the outer equator, and
     v grows down the outer wall, under the tube, up the inner wall and over
     the top.
   - A planar quad's front is -Y with width along +X and height along +Z.
  */
  namespace texture {
    //! Image right in UV space: +U, the direction of a vertex's tangent.
    inline constexpr Vec2 ImageRight { +1.0F, 0.0F };
    //! Image down in UV space: +V, the direction of a vertex's bitangent.
    inline constexpr Vec2 ImageDown { 0.0F, +1.0F };
  } // namespace texture

  //! Environment map conventions: cube maps and panoramas.
  /*!
   ### Cube maps

   - Faces are stored in D3D order +X, -X, +Y, -Y, +Z, -Z (array layers 0..5),
     each with its rows top first, in hardware cube space: +Y up, +Z forward.
     Cube space is read right-handed like the world, so +X lies on the LEFT
     of a viewer facing +Z.
     A face texel at UV (u, v) with p = 2 * (u, v) - 1 has the cube direction
     +X (1, -p.y, -p.x), -X (-1, -p.y, p.x), +Y (p.x, 1, p.y),
     -Y (p.x, -1, -p.y), +Z (p.x, -p.y, 1), -Z (-p.x, -p.y, -1).
   - A world direction (x, y, z) samples the cube direction (x, z, -y)
     (`CubeFromWorld`), a proper rotation: hardware +Y holds the world's up
     (+Z), +Z its forward (-Y) and -Z its back (+Y).
   - The cooker, the sky, sky-light capture, IBL filtering and the CPU static
     sky-light processor all use this mapping (shader side:
     `Shared/CubemapGeometry.hlsli`).
   - Authored six-face and cross sources use the Direct3D left-handed layout,
     which reads unmirrored only in a left-handed frame. The cooker mirrors
     each authored face about its vertical axis and swaps the +X and -X faces,
     so the stored cube reads unmirrored from inside.
   - An authored cube or sky rotation r (radians) turns the sampling
     direction by r about world +Z by the right-hand rule
     (`RotateDirectionAroundOxygenUp`), so the content turns by -r: clockwise
     seen from above for r > 0.

   ### Panoramas

   - An equirectangular panorama spans longitude across u and latitude down v,
     rows top first: v = 0 is straight up (+Z) and v = 1 straight down.
   - Longitude 0, the panorama's centre column (u = 0.5), faces the world's
     forward (-Y); u grows toward -X (u = 0.75), the right of a viewer inside
     facing the centre, and wraps through +Y at u = 0 and 1. A panorama
     therefore reads unmirrored from inside.
  */
  namespace environment {
    //! The hardware cube direction of a world direction.
    [[nodiscard]] constexpr auto CubeFromWorld(const Vec3& direction) noexcept
      -> Vec3
    {
      return { direction.x, direction.z, -direction.y };
    }

    //! The world direction of a hardware cube direction.
    [[nodiscard]] constexpr auto WorldFromCube(const Vec3& direction) noexcept
      -> Vec3
    {
      return { direction.x, -direction.z, direction.y };
    }
  } // namespace environment

} // namespace space

//! Physics-related constants. Use the same world space as the engine. Gravity
//! is always along -Z.
/*!
 Shapes: capsules, cylinders and cones are centred at the origin with their
 axis along +Z; a procedural cone's apex is at +Z. Physics `half_height` is
 half the straight cylinder length (capsule: tip-to-tip height is
 2 * (half_height + radius)) or half the total height (cylinder, cone).
 Procedural mesh `height` is the total tip-to-tip height. Metres.
*/
namespace physics {
  inline constexpr float GravityMagnitude = 9.80665F;
  inline constexpr Vec3 Gravity { 0.0F, 0.0F, -GravityMagnitude };
} // namespace physics

//! Color and light-unit conventions.
/*!
 ### Color

 - Rendering is linear HDR. Color textures (base color, emissive) are stored
   sRGB (`BC7UNormSRGB` by default) and decoded by the sampler; data textures
   (normal, ORM, roughness, metallic, occlusion, opacity) are linear. The
   G-buffer base color is `RGBA8UNormSRGB`.
 - The swap chain is not sRGB: tone mapping, and forward passes without
   `OXYGEN_HDR_OUTPUT`, encode sRGB in the shader.
 - Alpha: environment composition (sky, fog, local fog volumes, atmosphere)
   outputs premultiplied color and blends ONE, INV_SRC_ALPHA; translucent
   materials, composition and editor overlays output straight alpha and blend
   SRC_ALPHA, INV_SRC_ALPHA; lighting accumulates additively (ONE, ONE).

 ### Light units

 - Directional lights: illuminance in lux at the receiver.
 - Point and spot lights: luminous flux in lumens; intensity (candela) is the
   flux over the emitter's solid angle (4π for a point light, the cone's for
   a spot). Spot cone angles are half-angles from the axis, in radians.
 - Exposure: a camera's EV100 = log2(N² / t) - log2(ISO / 100) with aperture
   N and shutter time t. The exposure scale is
   2^(compensation - EV100) * key / 12.5, and the average luminance metered
   at EV100 is 0.18 * 2^EV100 (Oxygen/Core/Types/PostProcess.h).
*/
namespace color {
} // namespace color

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
