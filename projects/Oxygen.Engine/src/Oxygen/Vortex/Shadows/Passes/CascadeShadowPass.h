//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>
#include <unordered_map>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowFrameData.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/ShadowFrameBindings.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

class Renderer;

namespace shadows {

  class ShadowDepthPass;

  namespace internal {
    class CascadeShadowSetup;
    class ConventionalShadowTargetAllocator;
    class PointShadowSetup;
    class SpotShadowSetup;
  } // namespace internal

  class CascadeShadowPass {
  public:
    struct ViewShadowPassState {
      ShadowFrameData frame_data {};
      std::shared_ptr<graphics::Texture> shadow_surface;
      std::uint32_t shadow_caster_draw_count { 0U };
      std::uint32_t rendered_cascade_count { 0U };
      std::uint32_t rendered_draw_count { 0U };
    };

    struct ViewSpotShadowPassState {
      std::vector<ProjectedLocalShadowRecord> records;
      std::vector<LightSelectionIndex> quality_omissions;
      std::vector<std::shared_ptr<graphics::Texture>> shadow_surfaces;
      std::uint32_t shadow_caster_draw_count { 0U };
      std::uint32_t rendered_shadow_count { 0U };
      std::uint32_t rendered_draw_count { 0U };
    };

    struct ViewPointShadowPassState {
      std::vector<CubeLocalShadowRecord> records;
      std::vector<LightSelectionIndex> quality_omissions;
      std::vector<std::shared_ptr<graphics::Texture>> shadow_surfaces;
      std::uint32_t shadow_caster_draw_count { 0U };
      std::uint32_t rendered_shadow_count { 0U };
      std::uint32_t rendered_draw_count { 0U };
    };

    OXGN_VRTX_API explicit CascadeShadowPass(Renderer& renderer);
    OXGN_VRTX_API ~CascadeShadowPass();

    CascadeShadowPass(const CascadeShadowPass&) = delete;
    auto operator=(const CascadeShadowPass&) -> CascadeShadowPass& = delete;
    CascadeShadowPass(CascadeShadowPass&&) = delete;
    auto operator=(CascadeShadowPass&&) -> CascadeShadowPass& = delete;

    OXGN_VRTX_API auto OnFrameStart(
      frame::SequenceNumber sequence, frame::Slot slot) -> void;
    OXGN_VRTX_API auto RetainLocalSources(const PreparedViewShadowInput& view,
      std::span<const FrameLocalLightSelection> lights) -> void;
    OXGN_VRTX_API auto RetainDirectionalSources(
      std::span<const FrameDirectionalLightSelection> lights) -> void;
    [[nodiscard]] OXGN_VRTX_API auto RenderDirectionalView(
      const PreparedViewShadowInput& view_input,
      const FrameDirectionalLightSelection& directional_light,
      LightSelectionIndex selection_index) -> ViewShadowPassState;
    [[nodiscard]] OXGN_VRTX_API auto RenderSpotView(
      const PreparedViewShadowInput& view_input,
      std::span<const FrameLocalLightSelection> local_lights)
      -> ViewSpotShadowPassState;
    [[nodiscard]] OXGN_VRTX_API auto RenderPointView(
      const PreparedViewShadowInput& view_input,
      std::span<const FrameLocalLightSelection> local_lights)
      -> ViewPointShadowPassState;

  private:
    struct ViewQuality {
      std::uint64_t scene_generation { 0U };
      frame::SequenceNumber last_used { 0U };
      std::unordered_map<scene::NodeHandle, std::uint32_t> resolutions;
    };
    auto PrepareQualityHistory(const PreparedViewShadowInput& view,
      std::span<const FrameLocalLightSelection> lights) -> ViewQuality&;
    frame::SequenceNumber current_sequence_ { 0U };
    std::unordered_map<ViewId, ViewQuality> quality_history_;
    Renderer& renderer_;
    std::unique_ptr<internal::CascadeShadowSetup> cascade_setup_;
    std::unique_ptr<internal::SpotShadowSetup> spot_setup_;
    std::unique_ptr<internal::PointShadowSetup> point_setup_;
    std::unique_ptr<internal::ConventionalShadowTargetAllocator> allocator_;
    std::unique_ptr<ShadowDepthPass> depth_pass_;
  };

} // namespace shadows
} // namespace oxygen::vortex
