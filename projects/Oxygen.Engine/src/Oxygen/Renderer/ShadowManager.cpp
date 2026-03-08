//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/Internal/ConventionalShadowBackend.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Scene/Light/LightCommon.h>

namespace oxygen::renderer {

ShadowManager::ShadowManager(const observer_ptr<Graphics> gfx,
  const observer_ptr<ProviderT> provider,
  const observer_ptr<CoordinatorT> inline_transfers,
  const oxygen::ShadowQualityTier quality_tier)
  : gfx_(gfx)
  , staging_provider_(provider)
  , inline_transfers_(inline_transfers)
  , shadow_quality_tier_(quality_tier)
  , conventional_backend_(std::make_unique<internal::ConventionalShadowBackend>(
      gfx_, staging_provider_, inline_transfers_, shadow_quality_tier_))
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "expecting valid staging provider");
  DCHECK_NOTNULL_F(inline_transfers_, "expecting valid transfer coordinator");
}

ShadowManager::~ShadowManager() = default;

auto ShadowManager::OnFrameStart(RendererTag tag,
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  if (conventional_backend_) {
    conventional_backend_->OnFrameStart(tag, sequence, slot);
  }
}

auto ShadowManager::PublishForView(const ViewId view_id,
  const engine::ViewConstants& view_constants, const LightManager& lights,
  const std::span<const glm::vec4> shadow_caster_bounds,
  const SyntheticSunShadowInput* synthetic_sun_shadow) -> ShadowFramePublication
{
  std::vector<engine::DirectionalShadowCandidate> candidates_storage;
  const auto light_candidates = lights.GetDirectionalShadowCandidates();
  candidates_storage.assign(light_candidates.begin(), light_candidates.end());
  if (synthetic_sun_shadow != nullptr && synthetic_sun_shadow->enabled) {
    candidates_storage.push_back(engine::DirectionalShadowCandidate {
      .light_index = 0xFFFFFFFFU,
      .light_flags
      = static_cast<std::uint32_t>(engine::DirectionalLightFlags::kAffectsWorld
        | engine::DirectionalLightFlags::kCastsShadows
        | engine::DirectionalLightFlags::kSunLight
        | engine::DirectionalLightFlags::kEnvironmentContribution),
      .mobility = static_cast<std::uint32_t>(scene::LightMobility::kRealtime),
      .resolution_hint = synthetic_sun_shadow->resolution_hint,
      .direction_ws = synthetic_sun_shadow->direction_ws,
      .bias = synthetic_sun_shadow->bias,
      .normal_bias = synthetic_sun_shadow->normal_bias,
      .cascade_count = synthetic_sun_shadow->cascade_count,
      .distribution_exponent = synthetic_sun_shadow->distribution_exponent,
      .cascade_distances = synthetic_sun_shadow->cascade_distances,
    });
  }

  if (!conventional_backend_) {
    LOG_F(WARNING,
      "ShadowManager: no conventional backend is available for view {}",
      view_id.get());
    return {};
  }

  return conventional_backend_->PublishView(
    view_id, view_constants, candidates_storage, shadow_caster_bounds);
}

auto ShadowManager::SetPublishedViewFrameBindingsSlot(const ViewId view_id,
  const engine::BindlessViewFrameBindingsSlot slot) -> void
{
  if (conventional_backend_) {
    conventional_backend_->SetPublishedViewFrameBindingsSlot(view_id, slot);
  }
}

auto ShadowManager::TryGetFramePublication(const ViewId view_id) const noexcept
  -> const ShadowFramePublication*
{
  return conventional_backend_
    ? conventional_backend_->TryGetFramePublication(view_id)
    : nullptr;
}

auto ShadowManager::TryGetRasterRenderPlan(const ViewId view_id) const noexcept
  -> const RasterShadowRenderPlan*
{
  return conventional_backend_
    ? conventional_backend_->TryGetRasterRenderPlan(view_id)
    : nullptr;
}

auto ShadowManager::TryGetViewIntrospection(const ViewId view_id) const noexcept
  -> const ShadowViewIntrospection*
{
  return conventional_backend_
    ? conventional_backend_->TryGetViewIntrospection(view_id)
    : nullptr;
}

auto ShadowManager::GetConventionalShadowDepthTexture() const noexcept
  -> const std::shared_ptr<graphics::Texture>&
{
  if (conventional_backend_) {
    return conventional_backend_->GetDirectionalShadowTexture();
  }

  static const std::shared_ptr<graphics::Texture> kNullTexture {};
  return kNullTexture;
}

} // namespace oxygen::renderer
