//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Vortex/Types/ViewOverlay.h>

#include <EditorModule/NodeRegistry.h>

namespace oxygen::interop::module {

  //! The viewport tool that decides which gizmo the selection shows.
  enum class TransformTool : std::uint8_t {
    //! Selection only; no gizmo.
    kSelect = 0,
    kTranslate,
    kRotate,
    kScale,
  };

  //! The axes a translate or rotate gizmo follows. Scale always follows the
  //! active node's own axes: scaling along other axes would shear it.
  enum class TransformSpace : std::uint8_t {
    kWorld = 0,
    kLocal,
  };

  //! A gizmo part the pointer can grab.
  enum class GizmoHandle : std::uint8_t {
    kNone = 0,
    //! An axis arrow, ring or scale handle.
    kX,
    kY,
    kZ,
    //! A plane handle, named by the two axes it moves along.
    kXY,
    kXZ,
    kYZ,
    //! Translate in the view plane, or scale uniformly.
    kCenter,
    //! Rotate about the view direction.
    kView,
  };

  //! Increments a snapped drag rounds to.
  struct TransformSnap {
    bool enabled { false };
    //! Metres.
    float translation { 0.25F };
    float rotation_degrees { 15.0F };
    //! Scale factor step.
    float scale { 0.1F };
  };

  //! A ray in world space.
  struct GizmoRay {
    glm::vec3 origin { 0.0F };
    glm::vec3 direction { 0.0F, 1.0F, 0.0F };
  };

  //! What a view shows, in that view's pixels: the gizmo's hit tests, drags
  //! and geometry all project through it.
  struct GizmoCamera {
    glm::mat4 view { 1.0F };
    glm::mat4 projection { 1.0F };
    //! The rendered image inside the view, in pixels from its top-left.
    glm::vec2 viewport_origin { 0.0F };
    glm::vec2 viewport_size { 1.0F };

    //! Builds a camera from its matrices; the projection maps depth to [0, 1]
    //! in either direction.
    [[nodiscard]] static auto Create(const glm::mat4& view,
      const glm::mat4& projection, const glm::vec2& viewport_origin,
      const glm::vec2& viewport_size) -> GizmoCamera;

    [[nodiscard]] auto IsOrthographic() const noexcept -> bool;
    [[nodiscard]] auto Position() const noexcept -> glm::vec3;
    //! Unit view direction.
    [[nodiscard]] auto Forward() const noexcept -> glm::vec3;
    [[nodiscard]] auto Right() const noexcept -> glm::vec3;
    [[nodiscard]] auto Up() const noexcept -> glm::vec3;
    //! Unit direction from `point` toward the eye.
    [[nodiscard]] auto ToEye(const glm::vec3& point) const noexcept
      -> glm::vec3;
    //! Pixel position of `point`; none behind the eye.
    [[nodiscard]] auto Project(const glm::vec3& point) const
      -> std::optional<glm::vec2>;
    //! The ray through a pixel, pointing into the scene.
    [[nodiscard]] auto Ray(const glm::vec2& pixel) const -> GizmoRay;
    //! World length one pixel spans at `point`'s depth.
    [[nodiscard]] auto PixelSize(const glm::vec3& point) const -> float;
    //! The point under a pixel at mid-depth, inside the view volume whichever
    //! way depth runs; screen-space overlays are drawn through it.
    [[nodiscard]] auto Unproject(const glm::vec2& pixel) const -> glm::vec3;
    //! Device depth of `point`, as the depth buffer stores it.
    [[nodiscard]] auto DeviceDepth(const glm::vec3& point) const -> float;

  private:
    glm::mat4 view_projection_ { 1.0F };
    glm::mat4 inverse_view_projection_ { 1.0F };
    glm::mat4 inverse_view_ { 1.0F };
  };

  //! Where a gizmo stands and which axes it shows.
  struct GizmoFrame {
    glm::vec3 pivot { 0.0F };
    glm::quat orientation { 1.0F, 0.0F, 0.0F, 0.0F };

    //! Unit world direction of axis 0 (X), 1 (Y) or 2 (Z).
    [[nodiscard]] auto Axis(int index) const -> glm::vec3;
  };

  //! A manipulated node as it was when the drag began.
  struct GizmoTargetStart {
    UuidKey id {};
    glm::mat4 world { 1.0F };
    glm::mat4 parent_world { 1.0F };
    glm::vec3 local_position { 0.0F };
    glm::quat local_rotation { 1.0F, 0.0F, 0.0F, 0.0F };
    glm::vec3 local_scale { 1.0F };
  };

  //! A manipulated node's new local transform.
  struct GizmoTargetResult {
    UuidKey id {};
    glm::vec3 position { 0.0F };
    glm::quat rotation { 1.0F, 0.0F, 0.0F, 0.0F };
    glm::vec3 scale { 1.0F };
  };

  //! The value a drag applies, for the pointer readout.
  struct GizmoReadout {
    //! Bit 0, 1 and 2: X, Y and Z values are shown.
    std::uint8_t axes { 0U };
    //! Metres moved per axis, degrees turned (in `x`), or scale factors.
    glm::vec3 values { 0.0F };
  };

  //! Screen size of the gizmo before display scaling, in pixels.
  inline constexpr float kGizmoSizePixels = 110.0F;

  //! The handle under `pointer`, preferring the centre, then planes, then the
  //! nearest axis; hit areas are larger than the drawn handles.
  [[nodiscard]] auto HitTestGizmo(const GizmoCamera& camera,
    TransformTool tool, const GizmoFrame& frame, const glm::vec2& pointer,
    float display_scale) -> GizmoHandle;

  //! One drag of a gizmo handle: every update is computed from the starting
  //! transforms, so a drag never accumulates error and a cancel is exact.
  class GizmoDrag {
  public:
    //! Starts a drag; none when the handle cannot be dragged from this view
    //! (for example an axis seen end-on).
    /*!
     In World space translation snaps the pivot to the world grid; in Local
     space it snaps the offset along the gizmo axes. Rotation and scale snap
     the applied angle or factor in either space.
    */
    [[nodiscard]] static auto Begin(const GizmoCamera& camera,
      TransformTool tool, TransformSpace space, const GizmoFrame& frame,
      GizmoHandle handle,
      const glm::vec2& pointer, float display_scale,
      std::vector<GizmoTargetStart> targets) -> std::optional<GizmoDrag>;

    //! Applies the pointer; returns true when the results changed.
    /*!
     A pointer whose ray misses the drag plane keeps the last results. When
     any target's result cannot be represented by a position, rotation and
     scale (a rotation under a non-uniformly scaled parent shears), no target
     changes and `IsRepresentable` turns false until the pointer returns to a
     representable value.
    */
    auto Update(const GizmoCamera& camera, const glm::vec2& pointer,
      const TransformSnap& snap, bool invert_snap) -> bool;

    [[nodiscard]] auto Tool() const noexcept -> TransformTool { return tool_; }
    [[nodiscard]] auto Handle() const noexcept -> GizmoHandle
    {
      return handle_;
    }
    //! The gizmo as the drag currently places it.
    [[nodiscard]] auto Frame() const noexcept -> const GizmoFrame&
    {
      return frame_;
    }
    [[nodiscard]] auto StartFrame() const noexcept -> const GizmoFrame&
    {
      return start_frame_;
    }
    [[nodiscard]] auto Results() const noexcept
      -> const std::vector<GizmoTargetResult>&
    {
      return results_;
    }
    [[nodiscard]] auto Readout() const noexcept -> const GizmoReadout&
    {
      return readout_;
    }
    [[nodiscard]] auto IsRepresentable() const noexcept -> bool
    {
      return representable_;
    }
    [[nodiscard]] auto Pointer() const noexcept -> const glm::vec2&
    {
      return pointer_;
    }

    //! Rotation feedback: the plane normal and the start direction in it.
    [[nodiscard]] auto RotationNormal() const noexcept -> const glm::vec3&
    {
      return normal_;
    }
    [[nodiscard]] auto RotationStart() const noexcept -> const glm::vec3&
    {
      return reference_;
    }
    //! The signed, unwrapped angle the drag applies, in degrees.
    [[nodiscard]] auto AppliedDegrees() const noexcept -> float
    {
      return applied_degrees_;
    }

  private:
    GizmoDrag() = default;

    auto UpdateTranslate(const GizmoCamera& camera, bool snapped,
      const TransformSnap& snap) -> bool;
    auto UpdateRotate(const GizmoCamera& camera, bool snapped,
      const TransformSnap& snap) -> bool;
    auto UpdateScale(const GizmoCamera& camera, bool snapped,
      const TransformSnap& snap) -> bool;
    auto Commit(std::vector<GizmoTargetResult> results) -> bool;

    TransformTool tool_ { TransformTool::kTranslate };
    TransformSpace space_ { TransformSpace::kWorld };
    GizmoHandle handle_ { GizmoHandle::kNone };
    GizmoFrame start_frame_ {};
    GizmoFrame frame_ {};
    std::vector<GizmoTargetStart> targets_;
    std::vector<GizmoTargetResult> results_;
    GizmoReadout readout_ {};
    bool representable_ { true };
    float display_scale_ { 1.0F };
    glm::vec2 start_pointer_ { 0.0F };
    glm::vec2 pointer_ { 0.0F };
    glm::vec2 last_pointer_ { 0.0F };

    // The drag plane through the start pivot and where the pointer first hit
    // it; `normal_` is also the rotation axis.
    glm::vec3 normal_ { 0.0F, 0.0F, 1.0F };
    glm::vec3 start_hit_ { 0.0F };

    // Rotation: in-plane basis, unwrapped raw angle and the last direction.
    glm::vec3 reference_ { 1.0F, 0.0F, 0.0F };
    glm::vec3 binormal_ { 0.0F, 1.0F, 0.0F };
    float raw_radians_ { 0.0F };
    float last_angle_ { 0.0F };
    bool resync_angle_ { false };
    float applied_degrees_ { 0.0F };
  };

  //! What a view draws of the gizmo this frame.
  struct GizmoVisual {
    TransformTool tool { TransformTool::kTranslate };
    GizmoFrame frame {};
    GizmoHandle hovered { GizmoHandle::kNone };
    //! The drag in progress, in any view, or null.
    const GizmoDrag* drag { nullptr };
    float display_scale { 1.0F };
  };

  //! Adds the gizmo's geometry to `overlay`: idle handles to its scene layer,
  //! dimmed where hidden, and a drag's handle and feedback to its top layer.
  void BuildGizmoOverlay(const GizmoCamera& camera, const GizmoVisual& visual,
    vortex::ViewOverlay& overlay);

} // namespace oxygen::interop::module

#pragma managed(pop)
