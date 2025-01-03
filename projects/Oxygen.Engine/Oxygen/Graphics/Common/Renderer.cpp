//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Graphics/Common/Renderer.h"

#include "Oxygen/Graphics/Common/CommandList.h" // Needed to forward the command list ptr

using namespace oxygen;
using namespace oxygen::renderer;

void Renderer::OnInitialize(PlatformPtr platform, const RendererProperties& props)
{
  platform_ = std::move(platform);
  props_ = props;
  EmitRendererInitialized();
}

void Renderer::OnShutdown()
{
  EmitRendererShutdown();
  platform_.reset();
}

void Renderer::BeginFrame() const
{
  DLOG_F(2, "BEGIN Frame");
  EmitBeginFrameRender(current_frame_index_);
}

void Renderer::EndFrame() const
{
  EmitEndFrameRender(current_frame_index_);
  current_frame_index_ = (current_frame_index_ + 1) % kFrameBufferCount;
  DLOG_F(2, "END Frame");
}

void Renderer::Render(
  const resources::SurfaceId& surface_id,
  const RenderGameFunction& render_game) const
{
  // Remove the `const` from the object to allow the renderer to render. We
  // require const in the API so that the user does not inadvertently call
  // methods that are not marked as const when not provided with a mutable
  // renderer instance.
  auto* self = const_cast<Renderer*>(this);

  const auto& render_target = self->BeginFrame(surface_id);
  BeginFrame();

  auto command_lists = render_game(render_target);
  self->EndFrame(command_lists, surface_id);

  EndFrame();
}
