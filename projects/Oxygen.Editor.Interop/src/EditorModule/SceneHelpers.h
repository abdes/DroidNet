//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Vortex/Types/ViewOverlay.h>

#include <EditorModule/NodeRegistry.h>
#include <EditorModule/TransformGizmo.h>

namespace oxygen::interop::module {

  //! What a scene helper stands for.
  enum class SceneHelperKind : std::uint8_t {
    kDirectionalLight = 0,
    kPointLight,
    kSpotLight,
    kCamera,
  };

  //! A light or camera the viewport draws an icon and, when selected, a
  //! helper for. Editor overlay data, never authored or cooked.
  struct SceneHelperNode {
    UuidKey id {};
    SceneHelperKind kind { SceneHelperKind::kPointLight };
    //! World position of the node's origin.
    glm::vec3 position { 0.0F };
    //! Unit world direction a light emits along or a camera looks along.
    glm::vec3 direction { 0.0F, -1.0F, 0.0F };
    //! Linear light colour; white for cameras.
    glm::vec3 color { 1.0F };
    //! Metres; point and spot lights.
    float range { 0.0F };
    //! Half angles in radians; spot lights.
    float inner_cone { 0.0F };
    float outer_cone { 0.0F };
    //! A camera's viewing volume: the near plane corners, then the far plane
    //! corners, each bottom-left, bottom-right, top-right, top-left.
    std::array<glm::vec3, 8> frustum {};
    //! A camera's frustum is filled in for the view that draws it.
    bool has_frustum { false };
    bool selected { false };
    bool active { false };
    //! Its handles can be dragged: it is selected and not locked.
    bool editable { false };
  };

  //! A helper part the pointer can drag to edit its light.
  enum class HelperHandle : std::uint8_t {
    kNone = 0,
    //! A point or spot light's range.
    kRange,
    //! A spot light's inner and outer cone half angles.
    kInnerCone,
    kOuterCone,
  };

  //! A helper handle under the pointer.
  struct HelperHandleHit {
    //! Index into the helper nodes.
    std::size_t node { 0U };
    HelperHandle handle { HelperHandle::kNone };
  };

  //! An icon inside a picked rectangle.
  struct HelperIconHit {
    UuidKey id {};
    //! Pixels from the rectangle centre to the icon; zero when it covers it.
    float center_distance { 0.0F };
    //! Device depth of the node's origin.
    float depth { 0.0F };
  };

  //! An orientation triad axis.
  enum class TriadAxis : std::uint8_t {
    kNone = 0,
    kX,
    kY,
    kZ,
  };

  //! Icon diameter before display scaling, in pixels.
  inline constexpr float kHelperIconPixels = 30.0F;
  //! Smallest range and cone angles a handle drag produces.
  inline constexpr float kMinHelperRange = 0.01F;
  inline constexpr float kMinOuterConeRadians = 0.0087F;
  //! Largest cone half angle a handle drag produces: just under 90 degrees.
  inline constexpr float kMaxConeRadians = 1.5621F;
  //! Distance from the triad's centre to a letter facing the screen, before
  //! display scaling, in pixels.
  inline constexpr float kTriadAxisPixels = 28.0F;

  //! The icons whose discs overlap the rectangle between `rect_min` and
  //! `rect_max`, in view pixels; a click picks a few pixels around the
  //! pointer. Icons behind the eye are not hit.
  [[nodiscard]] auto PickHelperIcons(const GizmoCamera& camera,
    std::span<const SceneHelperNode> nodes, const glm::vec2& rect_min,
    const glm::vec2& rect_max, float display_scale)
    -> std::vector<HelperIconHit>;

  //! Draws every node's icon, tinted by its light colour, into the scene
  //! layer; selected icons get the selection accent.
  void BuildHelperIcons(const GizmoCamera& camera,
    std::span<const SceneHelperNode> nodes, float display_scale,
    vortex::ViewOverlay& overlay);

  //! Where a helper handle is drawn; none when the node has no such handle.
  [[nodiscard]] auto HelperHandlePosition(const GizmoCamera& camera,
    const SceneHelperNode& node, HelperHandle handle)
    -> std::optional<glm::vec3>;

  //! The editable handle under `pointer`, the nearest first; a point light's
  //! range can be grabbed anywhere on its silhouette circle.
  [[nodiscard]] auto HitTestHelperHandles(const GizmoCamera& camera,
    std::span<const SceneHelperNode> nodes, const glm::vec2& pointer,
    float display_scale) -> std::optional<HelperHandleHit>;

  //! One drag of a helper handle; every update is computed from the start,
  //! so a cancel restores the exact starting value.
  class HelperDrag {
  public:
    //! Starts a drag; none when the handle cannot be dragged from this view.
    [[nodiscard]] static auto Begin(const GizmoCamera& camera,
      const SceneHelperNode& node, HelperHandle handle,
      const glm::vec2& pointer) -> std::optional<HelperDrag>;

    //! Applies the pointer; returns true when the value changed. A pointer
    //! whose ray misses the drag plane keeps the last value.
    auto Update(const GizmoCamera& camera, const glm::vec2& pointer) -> bool;

    [[nodiscard]] auto Handle() const noexcept -> HelperHandle
    {
      return handle_;
    }
    //! The node as the drag currently shapes it.
    [[nodiscard]] auto Node() const noexcept -> const SceneHelperNode&
    {
      return node_;
    }
    //! Metres for the range, radians for a cone angle.
    [[nodiscard]] auto Value() const noexcept -> float { return value_; }
    [[nodiscard]] auto StartValue() const noexcept -> float
    {
      return start_value_;
    }
    [[nodiscard]] auto Pointer() const noexcept -> const glm::vec2&
    {
      return pointer_;
    }

  private:
    HelperDrag() = default;

    //! What the pointer's hit says about the dragged quantity.
    [[nodiscard]] auto Measure(const glm::vec3& hit) const -> float;
    void Apply(float value);

    SceneHelperNode node_ {};
    HelperHandle handle_ { HelperHandle::kNone };
    glm::vec3 plane_point_ { 0.0F };
    glm::vec3 plane_normal_ { 0.0F, 0.0F, 1.0F };
    float start_measure_ { 0.0F };
    float start_value_ { 0.0F };
    float value_ { 0.0F };
    glm::vec2 pointer_ { 0.0F };
  };

  //! What a view draws of the selected nodes' helpers this frame.
  struct HelperVisual {
    HelperHandleHit hovered {};
    bool has_hover { false };
    //! The drag in progress in any view, or null; its node is drawn as the
    //! drag shapes it.
    const HelperDrag* drag { nullptr };
    float display_scale { 1.0F };
  };

  //! Draws the selected nodes' helpers into the scene layer: a camera's
  //! viewing volume and frame, a directional light's arrow, a point light's
  //! range sphere and a spot light's cones, with the editable handles.
  void BuildSelectedHelpers(const GizmoCamera& camera,
    std::span<const SceneHelperNode> nodes, const HelperVisual& visual,
    vortex::ViewOverlay& overlay);

  //! The triad's centre in view pixels: the image's bottom-left corner.
  [[nodiscard]] auto TriadCenter(const GizmoCamera& camera,
    float display_scale) -> glm::vec2;

  //! The triad letter under `pointer`; the letter nearest the eye wins.
  [[nodiscard]] auto HitTestTriad(const GizmoCamera& camera,
    const glm::vec2& pointer, float display_scale) -> TriadAxis;

  //! Draws the orientation triad over everything: X, Y and Z letters in the
  //! axis colours, placed by the view's rotation.
  void BuildTriad(const GizmoCamera& camera, TriadAxis hovered,
    float display_scale, vortex::ViewOverlay& overlay);

} // namespace oxygen::interop::module

#pragma managed(pop)
