//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/ScenePrep/RenderItemData.h>
#include <Oxygen/Vortex/ScenePrep/ScenePrepState.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
class SceneTextures;

namespace sceneprep {
class ScenePrepPipeline;
} // namespace sceneprep

class InitViewsModule {
public:
  OXGN_VRTX_API explicit InitViewsModule(Renderer& renderer);
  OXGN_VRTX_API ~InitViewsModule();

  InitViewsModule(const InitViewsModule&) = delete;
  auto operator=(const InitViewsModule&) -> InitViewsModule& = delete;
  InitViewsModule(InitViewsModule&&) = delete;
  auto operator=(InitViewsModule&&) -> InitViewsModule& = delete;

  OXGN_VRTX_API void Execute(RenderContext& ctx, SceneTextures& scene_textures);

  [[nodiscard]] OXGN_VRTX_API auto GetPreparedSceneFrame(ViewId view_id) const
    -> const PreparedSceneFrame*;

private:
  struct PreparedSceneViewStorage {
    bool published { false };
    std::vector<sceneprep::RenderItemData> render_items;
    PreparedSceneFrame prepared_frame {};
  };

  Renderer& renderer_;
  std::unique_ptr<sceneprep::ScenePrepPipeline> scene_prep_;
  sceneprep::ScenePrepState scene_prep_state_ {};
  std::unordered_map<ViewId, PreparedSceneViewStorage> prepared_views_;
};

} // namespace oxygen::vortex
