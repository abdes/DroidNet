//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/OcclusionModule.h>

#include <cstdint>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>

namespace oxygen::vortex {

struct OcclusionModule::Impl {
  std::vector<std::uint8_t> visibility_storage;
  OcclusionFrameResults current_results
    = MakeInvalidOcclusionFrameResults(OcclusionFallbackReason::kStageDisabled);
  OcclusionStats stats {};

  auto PublishInvalid(RenderContext& ctx, OcclusionFallbackReason reason) -> void
  {
    visibility_storage.clear();
    current_results = MakeInvalidOcclusionFrameResults(reason);
    stats = OcclusionStats { .fallback_reason = reason };
    ctx.current_view.occlusion_results
      = observer_ptr<const OcclusionFrameResults> { &current_results };
  }

  auto PublishAllVisible(RenderContext& ctx, std::uint32_t draw_count,
    OcclusionFallbackReason reason) -> void
  {
    visibility_storage.assign(draw_count, 1U);
    current_results = OcclusionFrameResults {
      .visible_by_draw = std::span<const std::uint8_t> { visibility_storage },
      .draw_count = draw_count,
      .valid = true,
      .fallback_reason = reason,
    };
    stats = OcclusionStats {
      .draw_count = draw_count,
      .candidate_count = draw_count,
      .visible_count = draw_count,
      .fallback_reason = reason,
      .current_furthest_hzb_available
      = ctx.current_view.screen_hzb_available
        && ctx.current_view.screen_hzb_furthest_texture.get() != nullptr,
      .previous_results_valid = false,
      .results_valid = true,
    };
    ctx.current_view.occlusion_results
      = observer_ptr<const OcclusionFrameResults> { &current_results };
  }
};

namespace {

auto PreparedDrawCount(const PreparedSceneFrame& prepared_scene) noexcept
  -> std::uint32_t
{
  return static_cast<std::uint32_t>(prepared_scene.GetDrawMetadata().size());
}

} // namespace

OcclusionModule::OcclusionModule(Renderer& renderer, OcclusionConfig config)
  : renderer_(renderer)
  , config_(config)
  , impl_(std::make_unique<Impl>())
{
}

OcclusionModule::~OcclusionModule() = default;

void OcclusionModule::SetConfig(OcclusionConfig config) noexcept
{
  config_ = config;
}

auto OcclusionModule::GetConfig() const noexcept -> const OcclusionConfig&
{
  return config_;
}

void OcclusionModule::Execute(RenderContext& ctx, SceneTextures& scene_textures)
{
  (void)renderer_;
  (void)scene_textures;

  if (!config_.enabled) {
    impl_->PublishInvalid(ctx, OcclusionFallbackReason::kStageDisabled);
    return;
  }

  const auto* prepared_frame = ctx.current_view.prepared_frame.get();
  if (prepared_frame == nullptr) {
    impl_->PublishInvalid(ctx, OcclusionFallbackReason::kNoPreparedFrame);
    return;
  }

  const auto draw_count = PreparedDrawCount(*prepared_frame);
  if (draw_count == 0U) {
    impl_->PublishAllVisible(
      ctx, draw_count, OcclusionFallbackReason::kNoDraws);
    return;
  }

  if (!ctx.current_view.screen_hzb_available
    || ctx.current_view.screen_hzb_furthest_texture.get() == nullptr) {
    impl_->PublishAllVisible(
      ctx, draw_count, OcclusionFallbackReason::kNoCurrentFurthestHzb);
    return;
  }

  impl_->PublishAllVisible(
    ctx, draw_count, OcclusionFallbackReason::kNoPreviousResults);
}

auto OcclusionModule::GetCurrentResults() const noexcept
  -> const OcclusionFrameResults&
{
  return impl_->current_results;
}

auto OcclusionModule::GetStats() const noexcept -> const OcclusionStats&
{
  return impl_->stats;
}

} // namespace oxygen::vortex
