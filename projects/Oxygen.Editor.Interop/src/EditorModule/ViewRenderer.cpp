//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <EditorModule/RenderGraph.h>
#include <EditorModule/ViewRenderer.h>

namespace oxygen::interop::module {

ViewRenderer::ViewRenderer() = default;

ViewRenderer::~ViewRenderer()
{
  // Ensure we are unregistered?
  // We can't easily do it here because we need the Renderer instance.
  // Unregistration should be driven by the owner (EditorView).
  if (registered_) {
    LOG_F(WARNING, "ViewRenderer destroyed while still registered with Engine!");
  }
}

void ViewRenderer::SetFramebuffer(std::shared_ptr<graphics::Framebuffer> fb)
{
  if (!render_graph_) {
    render_graph_ = std::make_unique<RenderGraph>();
    render_graph_->SetupRenderPasses();
  }

  if (fb) {
    render_graph_->PrepareForRenderFrame(oxygen::observer_ptr<const graphics::Framebuffer>(fb.get()));
  }
}

void ViewRenderer::RegisterWithEngine(engine::Renderer& renderer, ViewId view_id, engine::ViewResolver resolver)
{
  if (registered_) {
    if (view_id_ != view_id) {
      LOG_F(ERROR, "ViewRenderer already registered with different ViewId!");
    }
    return;
  }

  view_id_ = view_id;

  engine::Renderer::RenderGraphFactory factory = [this](ViewId id, const engine::RenderContext& ctx, graphics::CommandRecorder& rec) -> co::Co<void> {
    if (render_graph_) {
      co_await render_graph_->RunPasses(ctx, rec);
    }
    co_return;
  };

  renderer.RegisterView(view_id, std::move(resolver), std::move(factory));

  registered_ = true;
  LOG_F(INFO, "ViewRenderer registered with Engine for ViewId {}", view_id.get());
}

void ViewRenderer::UnregisterFromEngine(engine::Renderer& renderer)
{
  if (!registered_) {
    return;
  }

  try {
    renderer.UnregisterView(view_id_);
  } catch (const std::exception& ex) {
    LOG_F(WARNING, "Failed to unregister view {}: {}", view_id_.get(), ex.what());
  }

  registered_ = false;
  view_id_ = ViewId {};
}

void ViewRenderer::Configure()
{
  // TODO: Update render pass configs
}

} // namespace oxygen::interop::module
