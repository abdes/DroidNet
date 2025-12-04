//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Scene/SceneNode.h>

#include "ViewRenderer.h"

namespace oxygen {
class Graphics;
}

namespace oxygen::examples::multiview {

struct ViewConfig {
  std::string name;
  std::string purpose;
  graphics::Color clear_color { 0.0f, 0.0f, 0.0f, 1.0f };
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
  virtual void RegisterRendererHooks(oxygen::engine::Renderer& renderer);

  // Prepare for rendering (configure renderer, register resolver/graph)
  virtual auto OnPreRender(oxygen::engine::Renderer& renderer) -> co::Co<void>
    = 0;

  // Composite the view to the backbuffer
  virtual void Composite(
    graphics::CommandRecorder& recorder, graphics::Texture& backbuffer)
    = 0;

  // Release resources
  virtual void ReleaseResources() = 0;

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
  virtual auto RenderToFramebuffer(const engine::RenderContext& render_ctx,
    graphics::CommandRecorder& recorder,
    const graphics::Framebuffer& framebuffer) -> co::Co<void>
    = 0;

  // Set graphics context for deferred resource release
  void SetGraphicsContext(std::weak_ptr<Graphics> graphics)
  {
    graphics_weak_ = graphics;
  }

protected:
  explicit DemoView(ViewConfig config, std::weak_ptr<Graphics> graphics = {});

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
  void RegisterView(const ViewPort& viewport, const Scissors& scissor);

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

private:
  // Context pointers set via SetRenderingContext
  engine::FrameContext* frame_context_ { nullptr };
  oxygen::Graphics* graphics_ { nullptr };
  const graphics::Surface* surface_ { nullptr };
  graphics::CommandRecorder* recorder_ {
    nullptr
  }; // Phase-specific, updated each frame
};

} // namespace oxygen::examples::multiview
