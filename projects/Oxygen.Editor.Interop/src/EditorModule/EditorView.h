//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Vortex/Renderer.h>

#include <EditorModule/EditorCameraPlacement.h>
#include <EditorModule/EditorViewportCameraControlMode.h>
#include <EditorModule/NodeRegistry.h>
#include <EditorModule/SurfaceRegistry.h>

namespace oxygen {
  class Graphics;
  namespace engine {
    struct RenderContext;
  } // namespace engine
  namespace vortex {
    class Renderer;
  } // namespace vortex
  namespace graphics {
    class CommandRecorder;
    class Framebuffer;
    class Texture;
    class Surface;
  } // namespace graphics
  namespace scene {
    class Scene;
  } // namespace scene
} // namespace oxygen

namespace oxygen::interop::module {

  //! Viewport camera view presets.
  enum class CameraViewPreset {
    //! Perspective view (free camera).
    kPerspective = 0,
    //! Top orthographic view.
    kTop,
    //! Bottom orthographic view.
    kBottom,
    //! Left orthographic view.
    kLeft,
    //! Right orthographic view.
    kRight,
    //! Front orthographic view.
    kFront,
    //! Back orthographic view.
    kBack,
  };

  //! Pose that places a scene node at a view's editor camera, in the node's
  //! parent space, using the editor's Euler convention.
  struct EditorCameraPose {
    glm::vec3 position { 0.0F };
    glm::quat rotation { 1.0F, 0.0F, 0.0F, 0.0F };
    //! `rotation` in the authoring Euler convention.
    glm::vec3 rotation_degrees { 0.0F };
    glm::vec3 scale { 1.0F };
    //! Orthographic half-height for an orthographic camera node: the piloted
    //! camera's own size, or the view's when it is orthographic.
    std::optional<float> orthographic_size;
    //! Vertical field of view, in degrees, for a perspective camera node when
    //! the view is perspective; none while that camera is piloted.
    std::optional<float> field_of_view_degrees;
  };

  //! The editor navigation camera's state: what a pane keeps when its view is
  //! released and what a new view starts from.
  struct EditorCameraState {
    glm::vec3 position { 0.0F };
    glm::quat rotation { 1.0F, 0.0F, 0.0F, 0.0F };
    //! Point the orbit navigation turns around.
    glm::vec3 focus_point { 0.0F };
    //! Half-height of the orthographic presets' view volume.
    float ortho_half_height { 10.0F };
  };

  //! What a pane renders: the lit scene or one of its diagnostic views.
  enum class EditorViewMode : std::uint8_t {
    kLit = 0,
    kUnlit,
    kWireframe,
    kLitWireframe,
    kDirectLighting,
    kIndirectLighting,
    kWorldNormals,
    kRoughness,
    kMetalness,
    kLinearDepth,
    kShadowMask,
  };

  //! How a pane presents the scene; editor view state, never authored data.
  struct EditorViewRenderOptions {
    EditorViewMode view_mode { EditorViewMode::kLit };
    bool show_grid { true };
    bool show_selection_outline { true };
  };

  //! What a frame request did to a view's editor camera.
  enum class EditorFramingOutcome : std::uint8_t {
    //! The editor camera is moving to frame the bounds.
    kFramed = 0,
    //! None of the requested nodes exist in the scene.
    kNothingToFrame,
    //! The view looks through a scene camera, which framing never moves.
    kViewingSceneCamera,
    //! The view does not exist.
    kNoView,
    //! The bounds are not finite; the view is unchanged.
    kInvalidBounds,
  };

  struct EditorViewContext {
    engine::FrameContext& frame_context;
    Graphics& graphics;
    graphics::CommandRecorder* recorder = nullptr; // Phase-specific!
  };

  enum class ViewState {
    kCreating,  // Resources being allocated
    kReady,     // Fully initialized, can render
    kHidden,    // Not rendering but resources retained
    kReleasing, // Resources being freed
    kDestroyed  // Fully cleaned up
  };

  class EditorView {
  public:
    struct Config {
      std::string name;
      std::string purpose;
      //! Key of the registered surface the view presents to.
      std::optional<SurfaceRegistry::GuidKey> compositing_target;

      // Initial extent; the view follows its surface's size once it has one.
      // Defaults are 1x1 to prevent invalid textures.
      uint32_t width = 1;
      uint32_t height = 1;
      graphics::Color clear_color{ 0.1f, 0.2f, 0.38f, 1.0f };

      //! Preset the editor camera starts with.
      CameraViewPreset camera_preset { CameraViewPreset::kPerspective };
      //! Editor camera state to start from instead of framing the scene, so
      //! the first presented frame already shows it.
      std::optional<EditorCameraState> editor_camera;
      //! Authored camera the view looks through from its first frame.
      std::optional<UuidKey> scene_camera;
      //! A camera preview inset composed over this host view's surface; the
      //! inset presents nothing on its own.
      std::optional<ViewId> inset_host;
      //! Presentation from the first frame; an inset always renders lit and
      //! without the grid.
      EditorViewRenderOptions render_options {};
    };

    explicit EditorView(Config config);
    ~EditorView();

    OXYGEN_MAKE_NON_COPYABLE(EditorView)
      OXYGEN_MAKE_NON_MOVABLE(EditorView)

      // Set rendering context (must be called before Initialize)
      void SetRenderingContext(const EditorViewContext& ctx);
    void
      ClearPhaseRecorder(); // Clear phase-specific pointers after OnSceneMutation

    // Phase hooks
    //! Binds the authored scene the view renders and creates the editor
    //! camera in `camera_scene`, which is editor-owned and outlives scene
    //! replacement.
    void Initialize(scene::Scene& scene, scene::Scene& camera_scene);
    //! Renders a replacement authored scene; the editor camera keeps its pose.
    void RetargetScene(scene::Scene& scene);
    void OnSceneMutation(); // Uses context from SetRenderingContext
    auto OnPreRender(vortex::Renderer& renderer) -> oxygen::co::Co<>;
    void EnsureRenderTarget(Graphics& graphics);

    // State management
    void Show();
    void Hide();
    void ReleaseResources();
    void Resize(uint32_t width, uint32_t height);

    [[nodiscard]] auto GetViewId() const -> ViewId;
    // Allow external owner (e.g. ViewManager) to set the engine-assigned ViewId
    // when the View is registered in FrameContext to avoid duplicate
    // registrations from EditorView::OnSceneMutation.
    void SetViewId(ViewId id) { view_id_ = id; }

    [[nodiscard]] auto GetName() const -> const std::string& {
      return config_.name;
    }
    [[nodiscard]] auto GetState() const -> ViewState;
    [[nodiscard]] auto IsVisible() const -> bool;
    //! The editor's own navigation camera for this view.
    [[nodiscard]] auto GetCameraNode() const -> scene::SceneNode;

    //! The editor camera's current state; none before the view is initialized.
    [[nodiscard]] auto GetEditorCameraState() const
      -> std::optional<EditorCameraState>;

    //! True for a camera preview inset composed over its host view.
    [[nodiscard]] auto IsInset() const noexcept -> bool {
      return config_.inset_host.has_value();
    }

    //! Renders the view through the authored camera on the scene node
    //! `node_id`, or through the editor camera when it is empty.
    /*!
     The node is resolved by id every frame: while it is missing or has no
     camera, the view falls back to the editor camera, and it returns to the
     authored camera when the node does (for example after an undo). The
     authored camera is never modified.
    */
    void SetSceneCamera(std::optional<UuidKey> node_id);

    //! True when this frame renders through an authored scene camera.
    [[nodiscard]] auto IsViewingSceneCamera() const -> bool;

    //! The camera node this view renders through this frame.
    [[nodiscard]] auto GetRenderCameraNode() const -> scene::SceneNode;

    //! Pilots the viewed scene camera: navigation drives the editor camera,
    //! which first takes the scene camera's world pose, and the scene camera
    //! follows it every frame. The view keeps rendering through the scene
    //! camera's own projection and framing.
    void SetPilotSceneCamera(bool pilot);

    //! True when navigation currently moves the viewed scene camera.
    [[nodiscard]] auto IsPilotingSceneCamera() const -> bool;

    //! Moves the piloted scene camera onto the editor camera's pose. Call after
    //! navigation has been applied for the frame.
    void SyncPilotedCamera();

    //! Pose that would place `node` at this view's editor camera.
    [[nodiscard]] auto ResolveEditorCameraPose(scene::SceneNode& node) const
      -> std::optional<EditorCameraPose>;
    [[nodiscard]] auto GetFramebuffer() const
      -> std::shared_ptr<graphics::Framebuffer> {
      return framebuffer_;
    }
    [[nodiscard]] auto GetConfig() const -> const Config& { return config_; }

    //! Changes how the view presents the scene from the next frame on.
    void SetRenderOptions(const EditorViewRenderOptions& options) noexcept {
      config_.render_options = options;
    }
    [[nodiscard]] auto GetWidth() const -> float { return width_; }
    [[nodiscard]] auto GetHeight() const -> float { return height_; }

    [[nodiscard]] auto GetFocusPoint() const noexcept -> const glm::vec3& {
      return focus_point_;
    }

    void SetFocusPoint(const glm::vec3& focus_point) noexcept {
      focus_point_ = focus_point;
    }

    //! Starts a short eased move of the editor camera that frames a world
    //! sphere with a 10% margin, keeping the view direction. Orthographic
    //! views resize to fit instead of moving closer.
    /*!
     Navigation input during the move takes over and ends it. A view looking
     through a scene camera does not move: framing never edits authored data.
    */
    [[nodiscard]] auto BeginFraming(const glm::vec3& center, float radius)
      -> EditorFramingOutcome;

    //! Advances a framing move by `dt_seconds`; call before navigation.
    void AdvanceFraming(float dt_seconds);

    //! Sets the camera to a predefined view preset.
    /*!
     Perspective keeps the current transform but ensures the camera component is
     a PerspectiveCamera.

     Orthographic presets (Top/Bottom/Left/Right/Front/Back) replace the camera
     component with an OrthographicCamera and align the camera transform to
     look at the current focus point.

     @param preset The preset to apply.
    */
    void SetCameraViewPreset(CameraViewPreset preset);

    //! Gets the last requested camera view preset.
    [[nodiscard]] auto GetCameraViewPreset() const noexcept -> CameraViewPreset {
      return camera_view_preset_;
    }

    //! Sets the editor camera navigation mode for this view.
    void SetCameraControlMode(EditorViewportCameraControlMode mode) noexcept;

    //! Gets the editor camera navigation mode for this view.
    [[nodiscard]] auto GetCameraControlMode() const noexcept
      -> EditorViewportCameraControlMode {
      return camera_control_mode_;
    }

    //! Sets the base movement speed used by fly camera navigation.
    void SetCameraMovementSpeed(float speed_units_per_second) noexcept;

    //! Gets the base movement speed used by fly camera navigation.
    [[nodiscard]] auto GetCameraMovementSpeed() const noexcept -> float {
      return camera_movement_speed_units_per_second_;
    }

    //! Updates camera lens and clipping settings on the current camera.
    void SetCameraViewSettings(
      float field_of_view_y_radians,
      float near_plane,
      float far_plane) noexcept;

  public:
    //! Gets the current orthographic half-height used to derive extents.
    auto GetOrthoHalfHeight() const noexcept -> float;

    //! Sets the orthographic half-height used to derive extents.
    auto SetOrthoHalfHeight(float half_height) noexcept -> void;

  private:
    void ResizeIfNeeded();
    void ResizeIfNeeded(Graphics& graphics);
    // Camera setup helpers (all scene mutations happen here)
    void CreateCamera(scene::Scene& camera_scene);
    void ApplyEditorCameraState(const EditorCameraState& state);
    void UpdateCameraForFrame();
    void ResolveSceneCamera(scene::Scene& scene);
    void BeginPilot();
    void SeatProxyOnSceneCamera();
    Config config_;
    ViewState state_{ ViewState::kCreating };
    bool visible_{ true };
    bool initial_orientation_set_{ false };
    bool initial_scene_frame_applied_{ false };
    float width_{ 0.0f };
    float height_{ 0.0f };

    glm::vec3 focus_point_{ 0.0f, 0.0f, 0.0f };

    CameraViewPreset camera_view_preset_{ CameraViewPreset::kPerspective };
    EditorViewportCameraControlMode camera_control_mode_{
      EditorViewportCameraControlMode::kOrbitTurntable };
    float camera_movement_speed_units_per_second_{ 5.0f };
    float camera_field_of_view_y_radians_{ 1.0471975803F };
    float camera_near_plane_{ 0.05F };
    float camera_far_plane_{ 1000.0F };
    bool camera_view_settings_overridden_{ false };
    float ortho_half_height_{ 10.0f };

    scene::SceneNode camera_node_;
    std::optional<UuidKey> scene_camera_id_;
    scene::SceneNode scene_camera_node_;
    bool pilot_requested_ { false };
    bool pilot_active_ { false };
    //! Local pose last written onto the piloted camera, to tell its own
    //! updates from authoring edits (undo, redo, Inspector).
    viewport::CameraPlacement pilot_written_ {};
    ViewId view_id_{ kInvalidViewId };

    //! An eased framing move of the editor camera.
    struct FramingMove {
      glm::vec3 start_position { 0.0F };
      glm::vec3 start_focus { 0.0F };
      float start_ortho_half_height { 0.0F };
      glm::vec3 target_position { 0.0F };
      glm::vec3 target_focus { 0.0F };
      float target_ortho_half_height { 0.0F };
      //! Position last written by the move: anything else means navigation
      //! moved the camera.
      glm::vec3 written_position { 0.0F };
      float elapsed { 0.0F };
    };
    std::optional<FramingMove> framing_;

    // Resources
    std::shared_ptr<graphics::Texture> color_texture_;
    std::shared_ptr<graphics::Texture> depth_texture_;
    std::shared_ptr<graphics::Framebuffer> framebuffer_;

    std::weak_ptr<Graphics> graphics_;
    std::weak_ptr<scene::Scene> scene_;
    std::weak_ptr<scene::Scene> camera_scene_;

    // Phase-specific context (valid only during OnSceneMutation)
    const EditorViewContext* current_context_{ nullptr };
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
