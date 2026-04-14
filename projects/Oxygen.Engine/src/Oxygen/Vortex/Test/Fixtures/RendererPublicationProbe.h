//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>

namespace oxygen::vortex::testing {

struct RendererPublicationProbe {
  static auto GetSceneRenderer(Renderer& renderer) -> SceneRenderer*
  {
    return renderer.scene_renderer_.get();
  }

  static auto GetSceneRenderer(const Renderer& renderer) -> const SceneRenderer*
  {
    return renderer.scene_renderer_.get();
  }

  static auto GetViewConstants(const Renderer& renderer) -> const ViewConstants&
  {
    return renderer.view_const_cpu_;
  }

  static auto GetViewConstantsManager(const Renderer& renderer)
    -> const internal::ViewConstantsManager*
  {
    return renderer.view_const_manager_.get();
  }

  static auto PopulateRenderContextViewState(Renderer& renderer,
    RenderContext& render_context, engine::FrameContext& context,
    const bool prefer_composite_source) -> void
  {
    renderer.PopulateRenderContextViewState(
      render_context, context, prefer_composite_source);
  }
};

} // namespace oxygen::vortex::testing
