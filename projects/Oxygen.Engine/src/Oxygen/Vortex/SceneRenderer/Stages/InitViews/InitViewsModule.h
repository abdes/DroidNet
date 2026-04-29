//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include <glm/vec4.hpp>

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

namespace resources {
class TextureBinder;
} // namespace resources

namespace upload {
class TransientStructuredBuffer;
} // namespace upload

class InitViewsModule {
public:
  struct PreparedSceneViewStorage {
    bool published { false };
    std::vector<sceneprep::RenderItemData> render_items;
    std::vector<DrawMetadata> draw_metadata;
    std::vector<float> world_matrices;
    std::vector<float> previous_world_matrices;
    std::vector<float> normal_matrices;
    std::vector<PreparedSceneFrame::PartitionRange> partitions;
    std::vector<glm::vec4> draw_bounding_spheres;
    std::vector<SkinnedPosePublication> current_skinned_pose_publications;
    std::vector<SkinnedPosePublication> previous_skinned_pose_publications;
    std::vector<MorphPublication> current_morph_publications;
    std::vector<MorphPublication> previous_morph_publications;
    std::vector<MaterialWpoPublication> current_material_wpo_publications;
    std::vector<MaterialWpoPublication> previous_material_wpo_publications;
    std::vector<MotionVectorStatusPublication>
      current_motion_vector_status_publications;
    std::vector<MotionVectorStatusPublication>
      previous_motion_vector_status_publications;
    std::vector<VelocityDrawMetadata> velocity_draw_metadata;
    PreparedSceneFrame prepared_frame {};
  };

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
  Renderer& renderer_;
  std::unique_ptr<resources::TextureBinder> texture_binder_ {};
  std::unique_ptr<upload::TransientStructuredBuffer> current_skinned_pose_buffer_;
  std::unique_ptr<upload::TransientStructuredBuffer> previous_skinned_pose_buffer_;
  std::unique_ptr<upload::TransientStructuredBuffer> current_morph_buffer_;
  std::unique_ptr<upload::TransientStructuredBuffer> previous_morph_buffer_;
  std::unique_ptr<upload::TransientStructuredBuffer> current_material_wpo_buffer_;
  std::unique_ptr<upload::TransientStructuredBuffer> previous_material_wpo_buffer_;
  std::unique_ptr<upload::TransientStructuredBuffer>
    current_motion_vector_status_buffer_;
  std::unique_ptr<upload::TransientStructuredBuffer>
    previous_motion_vector_status_buffer_;
  std::unique_ptr<upload::TransientStructuredBuffer> velocity_draw_metadata_buffer_;
  std::unique_ptr<sceneprep::ScenePrepPipeline> scene_prep_;
  sceneprep::ScenePrepState scene_prep_state_ {};
  std::unordered_map<ViewId, PreparedSceneViewStorage> prepared_views_;
};

} // namespace oxygen::vortex
