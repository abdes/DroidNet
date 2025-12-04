//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoView.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/DeferredObjectRelease.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/SceneCameraViewResolver.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::examples::multiview {

DemoView::DemoView(ViewConfig config, std::weak_ptr<Graphics> graphics)
  : config_(std::move(config))
  , graphics_weak_(graphics)
{
}

DemoView::~DemoView() { ReleaseResources(); }

void DemoView::EnsureCamera(scene::Scene& scene, std::string_view node_name)
{
  using oxygen::scene::PerspectiveCamera;
  using oxygen::scene::camera::ProjectionConvention;

  if (!camera_node_.IsAlive()) {
    camera_node_ = scene.CreateNode(std::string(node_name));
  }

  if (!camera_node_.HasCamera()) {
    auto camera
      = std::make_unique<PerspectiveCamera>(ProjectionConvention::kD3D12);
    const bool attached = camera_node_.AttachCamera(std::move(camera));
    CHECK_F(attached, "Failed to attach PerspectiveCamera to {}", node_name);
  }
}

void DemoView::UpdateCameraViewport(float width, float height)
{
  const auto cam_ref = camera_node_.GetCameraAs<scene::PerspectiveCamera>();
  if (cam_ref) {
    auto& cam = cam_ref->get();
    // Aspect ratio and viewport will be set by derived classes or default here?
    // Actually, aspect ratio depends on the specific view dimensions.
    // Let's set a default viewport here matching the dimensions.
    cam.SetViewport(ViewPort { .top_left_x = 0.0f,
      .top_left_y = 0.0f,
      .width = width,
      .height = height,
      .min_depth = 0.0f,
      .max_depth = 1.0f });
  }
}

void DemoView::RegisterView(const ViewPort& viewport, const Scissors& scissor)
{
  CHECK_NOTNULL_F(
    frame_context_, "frame_context must be set via SetRenderingContext");
  CHECK_NOTNULL_F(surface_, "surface must be set via SetRenderingContext");

  oxygen::View view;
  view.viewport = viewport;
  view.scissor = scissor;

  const auto metadata = oxygen::engine::ViewMetadata {
    .name = config_.name,
    .purpose = config_.purpose,
    .present_policy = oxygen::engine::PresentPolicy::Hidden,
  };

  if (view_id_.get() == 0) {
    LOG_F(INFO, "[{}] Registering view (fb={})", config_.name,
      static_cast<const void*>(framebuffer_.get()));
    view_id_ = frame_context_->RegisterView(oxygen::engine::ViewContext {
      .id = view_id_,
      .view = view,
      .metadata = metadata,
      .surface = std::ref(const_cast<graphics::Surface&>(*surface_)),
      .output = framebuffer_,
    });
  } else {
    frame_context_->UpdateView(view_id_,
      oxygen::engine::ViewContext {
        .id = view_id_,
        .view = view,
        .metadata = metadata,
        .surface = std::ref(const_cast<graphics::Surface&>(*surface_)),
        .output = framebuffer_,
      });
  }
}

void DemoView::RegisterRendererHooks(oxygen::engine::Renderer& renderer)
{
  if (view_id_.get() == 0) {
    LOG_F(WARNING, "[{}] ViewId not assigned; skipping renderer hooks",
      config_.name);
    return;
  }

  LOG_F(INFO, "[{}] Registering renderer hooks for view {}", config_.name,
    view_id_.get());

  renderer.RegisterViewResolverForView(view_id_,
    [view_ptr = this](
      const oxygen::engine::ViewContext& view_context) -> oxygen::ResolvedView {
      oxygen::renderer::SceneCameraViewResolver resolver(
        [view_ptr](
          const oxygen::ViewId&) { return view_ptr->GetCameraNode(); });
      return resolver(view_context.id);
    });

  renderer.RegisterRenderGraph(view_id_,
    [view_ptr = this](oxygen::ViewId view_id,
      const oxygen::engine::RenderContext& render_context,
      oxygen::graphics::CommandRecorder& recorder) -> co::Co<void> {
      if (!view_ptr->IsViewReady()) {
        LOG_F(INFO, "[{}] Skipping render graph; view {} not ready",
          view_ptr->config_.name, view_id.get());
        co_return;
      }

      const auto framebuffer = view_ptr->GetFramebuffer();
      if (!framebuffer) {
        LOG_F(WARNING, "[{}] Render graph missing framebuffer for view {}",
          view_ptr->config_.name, view_id.get());
        co_return;
      }

      co_await view_ptr->RenderToFramebuffer(
        render_context, recorder, *framebuffer);
      co_return;
    });
}

void DemoView::ReleaseResources()
{
  if (!framebuffer_ && !color_texture_ && !depth_texture_) {
    return;
  }

  LOG_F(INFO, "[{}] Releasing resources", config_.name);
  renderer_.ResetConfiguration();
  view_ready_ = false;
  recorder_ = nullptr; // Clear stale recorder pointer

  // Use deferred release for GPU resources to avoid freeing while in-flight.
  // Match OLDMainModule pattern: lock weak_ptr and pass directly to
  // DeferredObjectRelease.
  const auto gfx = graphics_weak_.lock();
  if (gfx) {
    try {
      if (color_texture_) {
        oxygen::graphics::DeferredObjectRelease(
          color_texture_, gfx->GetDeferredReclaimer());
      }

      if (depth_texture_) {
        oxygen::graphics::DeferredObjectRelease(
          depth_texture_, gfx->GetDeferredReclaimer());
      }

      if (framebuffer_) {
        oxygen::graphics::DeferredObjectRelease(
          framebuffer_, gfx->GetDeferredReclaimer());
      }
      return;
    } catch (const std::exception& ex) {
      LOG_F(WARNING, "[{}] Failed to defer release for resources: {}",
        config_.name, ex.what());
    }
  }

  // Fallback: immediate cleanup if graphics not available or deferred release
  // failed
  framebuffer_.reset();
  color_texture_.reset();
  depth_texture_.reset();
}

} // namespace oxygen::examples::multiview
