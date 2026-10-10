//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Hzb/ScreenHzbModule.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/OcclusionConfig.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Types/OcclusionStats.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
}

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
class SceneTextures;

//! Two-phase GPU occlusion culling of camera views.
/*!
 Per frame and view:

 1. `BuildPhase1` culls every draw against the frustum and publishes the
    view's visibility in `ctx.current_view.draw_visibility`. With occlusion
    on, only last frame's visible draws are drawn in phase 1.
 2. The depth prepass draws phase 1; the occlusion pyramid is built from it.
 3. `BuildPhase2` tests the rest against the pyramid, completes the
    visibility and writes the view's history.

 Each view keeps its own history slots, history and counters. A history
 reset (camera cut, projection or view rect change, new view) makes phase 1
 draw every draw in the frustum for one frame.
*/
class OcclusionModule {
public:
  OXGN_VRTX_API explicit OcclusionModule(
    Renderer& renderer, OcclusionConfig config = {});
  OXGN_VRTX_API ~OcclusionModule();

  OXYGEN_MAKE_NON_COPYABLE(OcclusionModule)
  OXYGEN_MAKE_NON_MOVABLE(OcclusionModule)

  OXGN_VRTX_API void SetConfig(OcclusionConfig config) noexcept;
  [[nodiscard]] OXGN_VRTX_API auto GetConfig() const noexcept
    -> const OcclusionConfig&;

  //! Records phase 1 of the current view and publishes its visibility.
  /*!
   Without a prepared frame or resolved view, the published visibility is
   invalid and every pass keeps all of its candidates.
  */
  OXGN_VRTX_API void BuildPhase1(RenderContext& ctx,
    graphics::CommandRecorder& recorder, const SceneTextures& scene_textures);

  //! Whether the current view needs the occlusion pyramid and phase 2.
  [[nodiscard]] OXGN_VRTX_API auto NeedsPhase2(const RenderContext& ctx) const
    -> bool;

  //! Records phase 2 of the current view against `pyramid`, built from its
  //! phase 1 depth. Without a pyramid every draw in the frustum is visible.
  OXGN_VRTX_API void BuildPhase2(RenderContext& ctx,
    graphics::CommandRecorder& recorder,
    const std::optional<ScreenHzbModule::OcclusionPyramid>& pyramid);

  //! Forgets a removed view's history and counters.
  OXGN_VRTX_API void RemoveViewState(ViewId view_id);

  //! The latest counters read back for a view, frames late.
  [[nodiscard]] OXGN_VRTX_API auto GetStats(ViewId view_id) const
    -> std::optional<OcclusionStats>;

private:
  struct Impl;

  OcclusionConfig config_ {};
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::vortex
