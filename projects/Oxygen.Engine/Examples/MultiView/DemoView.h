//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Scene/SceneNode.h>

#include "MultiView/ViewRenderer.h"

namespace oxygen {
class Graphics;
namespace graphics {
  class Surface;
  class CommandRecorder;
  class Texture;
  class Framebuffer;
} // namespace graphics
namespace engine {
  class FrameContext;
  class Renderer;
  struct RenderContext;
} // namespace engine
namespace imgui {
  class ImGuiModule;
} // namespace imgui
namespace scene {
  class Scene;
} // namespace scene
} // namespace oxygen

namespace oxygen::examples::multiview {

struct ViewConfig {
  std::string name;
  std::string purpose;
  graphics::Color clear_color { 0.0F, 0.0F, 0.0F, 1.0F };
  bool wireframe { false };
};

//! Context passed to views during scene mutation and rendering phases.
/*!
 Contains references to core rendering resources that are stable throughout
 the frame and known early in MainModule. Views should query this context
 rather than being passed individual parameters.

 Pointer semantics:
 - References: Stable for the frame's lifetime
 - graphics (observer_ptr via reference): Transient handle, never stored
 - surface (reference): Valid for the frame
 - recorder (reference): Valid for the current phase
*/
struct DemoViewContext {
  engine::FrameContext& frame_context;
  oxygen::Graphics& graphics;
  const graphics::Surface& surface;
  graphics::CommandRecorder& recorder;

  // Derived/computed properties that views can query
  [[nodiscard]] auto surface_width() const -> uint32_t
  {
    return surface.Width();
  }

  [[nodiscard]] auto surface_height() const -> uint32_t
  {
    return surface.Height();
  }
};

class DemoView {
public:
  virtual ~DemoView();

  OXYGEN_MAKE_NON_COPYABLE(DemoView)
  OXYGEN_DEFAULT_MOVABLE(DemoView)

  // Set the rendering context early. Must be called before Initialize.
  // The recorder is valid ONLY during the current phase (OnSceneMutation).
  // Do NOT use recorder after this phase ends.
  void SetRenderingContext(const DemoViewContext& ctx)
  {
    frame_context_ = &ctx.frame_context;
    graphics_ = &ctx.graphics;
    surface_ = &ctx.surface;
    recorder_ = &ctx.recorder; // Valid ONLY during OnSceneMutation phase
  }

  // Clear the phase-specific recorder after OnSceneMutation is complete
  void ClearPhaseRecorder() { recorder_ = nullptr; }

  // Initialize the view (create camera, etc.)
  // Called once per view after rendering context is set.
  virtual void Initialize(scene::Scene& scene) = 0;

  // Handle scene mutation (update camera, resize resources, register view)
  // Uses context set via SetRenderingContext.
  virtual void OnSceneMutation() = 0;

  //! Register resolver and render graph hooks with the shared renderer.
  virtual void RegisterViewForRendering(oxygen::engine::Renderer& renderer);

  // Prepare for rendering (configure renderer, register resolver/graph)
  virtual auto OnPreRender(oxygen::engine::Renderer& renderer) -> co::Co<> = 0;

  // Composite the view to the backbuffer
  virtual void Composite(
    graphics::CommandRecorder& recorder, graphics::Texture& backbuffer)
    = 0;

  //! Render ImGui after compositing (optional per view).
  auto RenderGuiAfterComposite(graphics::CommandRecorder& recorder,
    const graphics::Framebuffer& framebuffer) -> co::Co<>
  {
    return renderer_.RenderGuiAfterComposite(recorder, framebuffer);
  }

  [[nodiscard]] auto IsGuiEnabled() const -> bool
  {
    return renderer_.IsGuiEnabled();
  }

  void SetImGuiModule(observer_ptr<imgui::ImGuiModule> module)
  {
    renderer_.SetImGuiModule(module);
  }

  // Release resources. Public non-virtual entry point that must be called
  // while the object is still fully alive. This will call the protected
  // virtual hook `OnReleaseResources()` so derived types can release
  // derived-only state and schedule any deferred releases while the
  // derived object still exists.
  void ReleaseResources();

  [[nodiscard]] auto GetViewId() const -> ViewId { return view_id_; }

  // Get camera node for resolver
  [[nodiscard]] auto GetCameraNode() const -> scene::SceneNode
  {
    return camera_node_;
  }

  // Check if view is ready for rendering
  [[nodiscard]] auto IsViewReady() const -> bool { return view_ready_; }

  // Get framebuffer for rendering
  [[nodiscard]] auto GetFramebuffer() const
    -> std::shared_ptr<graphics::Framebuffer>
  {
    return framebuffer_;
  }

  // Render view to its framebuffer
  virtual auto RenderFrame(const engine::RenderContext& render_ctx,
    graphics::CommandRecorder& recorder) -> co::Co<>
    = 0;

  // Set graphics context for deferred resource release
  void SetGraphicsContext(std::weak_ptr<Graphics> graphics)
  {
    graphics_weak_ = std::move(graphics);
  }

protected:
  explicit DemoView(ViewConfig config, std::weak_ptr<Graphics> graphics = {});

  // Hook called by ReleaseResources() when object is alive. Derived
  // implementations must override this instead of ReleaseResources().
  // Implementations should schedule deferred releases for any derived
  // resources using `graphics_weak_` exactly as the base class does.
  virtual void OnReleaseResources();

  // Getters for context (safe to use after SetRenderingContext is called)
  [[nodiscard]] auto GetFrameContext() const -> engine::FrameContext&
  {
    return *frame_context_;
  }

  [[nodiscard]] auto GetGraphics() const -> oxygen::Graphics&
  {
    return *graphics_;
  }

  [[nodiscard]] auto GetSurface() const -> const graphics::Surface&
  {
    return *surface_;
  }

  [[nodiscard]] auto GetRecorder() const -> graphics::CommandRecorder&
  {
    CHECK_NOTNULL_F(recorder_, "Recorder must be set via SetRenderingContext");
    return *recorder_;
  }

  // Common helpers
  void EnsureCamera(scene::Scene& scene, std::string_view node_name);
  void UpdateCameraViewport(float width, float height);
  void AddViewToFrameContext(const ViewPort& viewport, const Scissors& scissor);

  // Per-view renderer is now responsible for registering/unregistering with
  // the engine Renderer. No need to store engine renderer pointer here.

  // Protected accessors for derived classes (prefer using these instead of
  // manipulating members directly so the base class can evolve safely)
  [[nodiscard]] auto Config() const -> const ViewConfig& { return config_; }

  [[nodiscard]] auto RendererRef() -> ViewRenderer& { return renderer_; }

  [[nodiscard]] auto ColorTextureRef() -> std::shared_ptr<graphics::Texture>&
  {
    return color_texture_;
  }

  [[nodiscard]] auto DepthTextureRef() -> std::shared_ptr<graphics::Texture>&
  {
    return depth_texture_;
  }

  [[nodiscard]] auto FramebufferRef() -> std::shared_ptr<graphics::Framebuffer>&
  {
    return framebuffer_;
  }

  [[nodiscard]] auto CameraNodeRef() -> scene::SceneNode&
  {
    return camera_node_;
  }

  // Prefer using this setter to change the ready state of the view
  void SetViewReady(bool ready) { view_ready_ = ready; }

private:
  // Context pointers set via SetRenderingContext
  engine::FrameContext* frame_context_ { nullptr };
  oxygen::Graphics* graphics_ { nullptr };
  const graphics::Surface* surface_ { nullptr };
  graphics::CommandRecorder* recorder_ {
    nullptr
  }; // Phase-specific, updated each frame

  // Tracks whether ReleaseResources() was run. Destructor will avoid
  // calling virtual methods and will only run base fallback cleanup if
  // this flag is false.
  bool resources_released_ { false };

  // Base-owned properties (moved from protected to private) - use the
  // protected accessors above when implementing derived views.
  ViewConfig config_;
  ViewId view_id_ {};
  scene::SceneNode camera_node_;
  std::weak_ptr<Graphics> graphics_weak_;

  // Resources
  std::shared_ptr<graphics::Texture> color_texture_;
  std::shared_ptr<graphics::Texture> depth_texture_;
  std::shared_ptr<graphics::Framebuffer> framebuffer_;
  ViewRenderer renderer_;
  bool view_ready_ { false };

  // Helper that performs non-virtual base cleanup and schedules deferred
  // release for base-owned GPU resources. This avoids duplicating the
  // logic across the destructor and ReleaseResources().
  void BaseDeferredRelease();
};

} // namespace oxygen::examples::multiview
