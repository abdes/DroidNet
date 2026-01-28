//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/Runtime/SingleViewModuleBase.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/SceneCameraViewResolver.h>

namespace oxygen::examples {

SingleViewModuleBase::SingleViewModuleBase(const DemoAppContext& app)
  : DemoModuleBase(app)
{
  try {
    auto& rg = AddComponent<RenderGraph>(app);
    render_graph_ = oxygen::observer_ptr<RenderGraph>(&rg);
  } catch (const std::exception& ex) {
    LOG_F(WARNING, "SingleViewModuleBase: failed to create RenderGraph: {}",
      ex.what());
  }
}

void SingleViewModuleBase::OnShutdown() noexcept
{
  UnregisterViewForRendering("module shutdown");
}

auto SingleViewModuleBase::ClearBackbufferReferences() -> void
{
  if (!render_graph_) {
    return;
  }

  try {
    render_graph_->ClearBackbufferReferences();
  } catch (const std::exception& ex) {
    LOG_F(WARNING, "ClearBackbufferReferences() threw: {}", ex.what());
  }
}

auto SingleViewModuleBase::UpdateFrameContext(
  engine::FrameContext& context, ViewReadyCallback on_view_ready) -> void
{
  const bool has_view = app_window_ && app_window_->GetWindow()
    && !app_window_->GetSurface().expired();

  if (!has_view) {
    if (view_id_ != ViewId { kInvalidViewId }) {
      UnregisterViewForRendering("window or surface unavailable");
      context.RemoveView(view_id_);
      view_id_ = ViewId { kInvalidViewId };
    }
    return;
  }

  auto surface = app_window_->GetSurface().lock();
  if (!surface) {
    if (view_id_ != ViewId { kInvalidViewId }) {
      UnregisterViewForRendering("surface lock failed");
      context.RemoveView(view_id_);
      view_id_ = ViewId { kInvalidViewId };
    }
    return;
  }

  ViewPort viewport;
  viewport.top_left_x = 0.0f;
  viewport.top_left_y = 0.0f;
  viewport.width = static_cast<float>(surface->Width());
  viewport.height = static_cast<float>(surface->Height());
  viewport.min_depth = 0.0f;
  viewport.max_depth = 1.0f;

  Scissors scissors;
  scissors.left = 0;
  scissors.top = 0;
  scissors.right = static_cast<int32_t>(surface->Width());
  scissors.bottom = static_cast<int32_t>(surface->Height());

  View view;
  view.viewport = viewport;
  view.scissor = scissors;

  engine::ViewContext view_ctx {
    .view = view,
    .metadata = {
      .name = "MainView",
      .purpose = "primary",
    },
  };

  auto fb_weak = app_window_->GetCurrentFrameBuffer();
  view_ctx.output
    = observer_ptr { fb_weak.expired() ? nullptr : fb_weak.lock().get() };

  if (view_id_ == ViewId { kInvalidViewId }) {
    view_id_ = context.RegisterView(std::move(view_ctx));
  } else {
    context.UpdateView(view_id_, std::move(view_ctx));
  }

  if (on_view_ready) {
    on_view_ready(
      static_cast<int>(surface->Width()), static_cast<int>(surface->Height()));
  }
}

auto SingleViewModuleBase::RegisterViewForRendering(
  scene::SceneNode camera_node) -> void
{
  if (renderer_view_registered_) {
    return;
  }
  if (view_id_ == ViewId { kInvalidViewId }) {
    return;
  }

  auto* renderer = ResolveRenderer();
  if (!renderer) {
    DLOG_F(1, "Renderer unavailable; deferring view registration");
    return;
  }

  if (!render_graph_) {
    LOG_F(ERROR, "RenderGraph unavailable; cannot register view {}",
      view_id_.get());
    return;
  }

  renderer->RegisterView(
    view_id_,
    [camera_node](const engine::ViewContext& vc) -> ResolvedView {
      renderer::SceneCameraViewResolver resolver(
        [camera_node](const ViewId& /*id*/) { return camera_node; });
      return resolver(vc.id);
    },
    [this](ViewId id, const engine::RenderContext& rc,
      graphics::CommandRecorder& rec) -> co::Co<void> {
      (void)id;

      if (!render_graph_) {
        co_return;
      }

      if (rc.framebuffer) {
        render_graph_->PrepareForRenderFrame(rc.framebuffer);
      }

      co_await render_graph_->RunPasses(rc, rec);

      auto imgui_module_ref = app_.engine->GetModule<imgui::ImGuiModule>();
      if (imgui_module_ref) {
        auto& imgui_module = imgui_module_ref->get();
        auto imgui_pass = imgui_module.GetRenderPass();
        if (imgui_pass) {
          co_await imgui_pass->Render(rec);
        }
      }
    });

  renderer_view_registered_ = true;
  LOG_F(INFO, "Registered renderer view {}", view_id_.get());
}

auto SingleViewModuleBase::UnregisterViewForRendering(std::string_view reason)
  -> void
{
  if (!renderer_view_registered_) {
    return;
  }

  auto* renderer = ResolveRenderer();
  if (renderer && view_id_ != ViewId { kInvalidViewId }) {
    renderer->UnregisterView(view_id_);
    LOG_F(INFO, "Unregistered renderer view {} ({})", view_id_.get(), reason);
  } else {
    LOG_F(INFO, "Renderer view cleanup skipped ({})", reason);
  }

  renderer_view_registered_ = false;
}

auto SingleViewModuleBase::ResolveRenderer() const -> oxygen::engine::Renderer*
{
  if (!app_.engine) {
    return nullptr;
  }

  auto renderer_module = app_.engine->GetModule<engine::Renderer>();
  if (!renderer_module) {
    return nullptr;
  }

  return &(renderer_module->get());
}

} // namespace oxygen::examples
