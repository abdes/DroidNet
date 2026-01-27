//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/DeferredObjectRelease.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/SceneCameraViewResolver.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>

#include <utility>

#include "MultiView/DemoView.h"

namespace oxygen::examples::multiview {

DemoView::DemoView(ViewConfig config, std::weak_ptr<Graphics> graphics)
  : config_(std::move(config))
  , graphics_weak_(std::move(graphics))
{
}

DemoView::~DemoView()
{
  if (!resources_released_) {
    LOG_F(WARNING,
      "[{}] ReleaseResources() was not called before destruction; performing "
      "base-only deferred cleanup",
      config_.name);
    // Perform non-virtual base-only cleanup. This avoids calling virtual
    // methods during destruction and ensures base resources get cleaned.
    // (Derived state cannot be safely touched here.)
    BaseDeferredRelease();
  }
}

void DemoView::EnsureCamera(scene::Scene& scene, std::string_view node_name)
{
  using scene::PerspectiveCamera;

  if (!camera_node_.IsAlive()) {
    camera_node_ = scene.CreateNode(std::string(node_name));
  }

  if (!camera_node_.HasCamera()) {
    auto camera = std::make_unique<PerspectiveCamera>();
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
    cam.SetViewport(ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = width,
      .height = height,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    });
  }
}

void DemoView::AddViewToFrameContext(
  const ViewPort& viewport, const Scissors& scissor)
{
  CHECK_NOTNULL_F(
    frame_context_, "frame_context must be set via SetRenderingContext");
  CHECK_NOTNULL_F(surface_, "surface must be set via SetRenderingContext");

  View view;
  view.viewport = viewport;
  view.scissor = scissor;

  const auto metadata = engine::ViewMetadata {
    .name = config_.name,
    .purpose = config_.purpose,
  };

  if (view_id_.get() == 0) {
    LOG_F(INFO, "[{}] Registering view (fb={})", config_.name,
      static_cast<const void*>(framebuffer_.get()));
    view_id_ = frame_context_->RegisterView(engine::ViewContext {
      .id = view_id_,
      .view = view,
      .metadata = metadata,
    });
  } else {
    frame_context_->UpdateView(view_id_,
      engine::ViewContext {
        .id = view_id_,
        .view = view,
        .metadata = metadata,
        .output = observer_ptr { framebuffer_.get() },
      });
  }
}

void DemoView::RegisterViewForRendering(engine::Renderer& renderer)
{
  if (view_id_.get() == 0) {
    LOG_F(WARNING, "[{}] ViewId not assigned; skipping renderer hooks",
      config_.name);
    return;
  }

  LOG_F(INFO, "[{}] Registering renderer hooks for view {}", config_.name,
    view_id_.get());

  // Ask our per-view renderer to register with the engine for this view id.
  renderer_.RegisterWithEngine(renderer, view_id_,
    [view_ptr = this](const engine::ViewContext& view_context) -> ResolvedView {
      renderer::SceneCameraViewResolver resolver(
        [view_ptr](const ViewId&) { return view_ptr->GetCameraNode(); });
      return resolver(view_context.id);
    });
}

void DemoView::ReleaseResources()
{
  if (resources_released_) {
    return;
  }

  LOG_F(INFO, "[{}] Releasing resources", config_.name);

  // Allow derived classes to run their cleanup while object is still alive.
  // Derived overrides should schedule deferred releases for any derived
  // resources if needed.
  OnReleaseResources();

  // Base cleanup: reset renderer state and phase-specific pointers.
  renderer_.ResetConfiguration();
  // Unregister our view when releasing resources so the renderer doesn't
  // retain stale resolvers / graph factories for this view id.
  if (view_id_.get() != 0) {
    renderer_.UnregisterFromEngine();
    view_id_ = ViewId {};
  }
  view_ready_ = false;
  recorder_ = nullptr; // Clear stale recorder pointer

  // Use deferred release for GPU resources to avoid freeing while in-flight.
  BaseDeferredRelease();
  resources_released_ = true;
}

void DemoView::OnReleaseResources()
{
  // Default: no-op. Derived classes should override when they have
  // additional resources to release. Important: any deferred release of
  // derived resources should use `graphics_weak_` and DeferredObjectRelease
  // to match the base class behavior.
}

void DemoView::BaseDeferredRelease()
{
  using graphics::DeferredObjectRelease;

  if (const auto gfx = graphics_weak_.lock()) {
    try {
      if (color_texture_) {
        DeferredObjectRelease(color_texture_, gfx->GetDeferredReclaimer());
      }

      if (depth_texture_) {
        DeferredObjectRelease(depth_texture_, gfx->GetDeferredReclaimer());
      }

      if (framebuffer_) {
        DeferredObjectRelease(framebuffer_, gfx->GetDeferredReclaimer());
      }
      return;
    } catch (const std::exception& ex) {
      LOG_F(WARNING, "[{}] Failed to defer release for resources: {}",
        config_.name, ex.what());
      // Fallback to immediate reset
      framebuffer_.reset();
      color_texture_.reset();
      depth_texture_.reset();
    }
  }

  // Fallback: immediate cleanup if graphics not available
  framebuffer_.reset();
  color_texture_.reset();
  depth_texture_.reset();
}

} // namespace oxygen::examples::multiview
