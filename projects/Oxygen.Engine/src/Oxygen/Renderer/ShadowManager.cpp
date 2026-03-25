//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <optional>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/Internal/ConventionalShadowBackend.h>
#include <Oxygen/Renderer/ShadowManager.h>

namespace oxygen::renderer {

namespace {

  auto IsSceneSunShadowCandidate(
    const engine::DirectionalShadowCandidate& candidate) -> bool
  {
    const auto flags
      = static_cast<engine::DirectionalLightFlags>(candidate.light_flags);
    return (flags & engine::DirectionalLightFlags::kSunLight)
      != engine::DirectionalLightFlags::kNone;
  }

  auto RecordDirectionalImplementation(
    std::unordered_map<std::uint64_t, engine::ShadowImplementationKind>&
      history,
    const ViewId view_id, const engine::ShadowImplementationKind implementation,
    const std::uint32_t candidate_count) -> void
  {
    const std::uint64_t key = view_id.get();
    const auto it = history.find(key);
    if (it != history.end() && it->second == implementation) {
      return;
    }

    history[key] = implementation;
    switch (implementation) {
    case engine::ShadowImplementationKind::kConventional:
      LOG_F(INFO,
        "ShadowManager: view {} is using conventional directional shadows "
        "(eligible_candidates={})",
        key, candidate_count);
      break;
    default:
      LOG_F(INFO,
        "ShadowManager: view {} has no active directional shadow backend "
        "(eligible_candidates={})",
        key, candidate_count);
      break;
    }
  }

} // namespace

ShadowManager::ShadowManager(const observer_ptr<Graphics> gfx,
  const observer_ptr<ProviderT> provider,
  const observer_ptr<CoordinatorT> inline_transfers,
  const oxygen::ShadowQualityTier quality_tier,
  const oxygen::DirectionalShadowImplementationPolicy directional_policy)
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
  CHECK_F(directional_policy
      == oxygen::DirectionalShadowImplementationPolicy::kConventionalOnly,
    "ShadowManager: only conventional directional shadows are supported");
}

ShadowManager::~ShadowManager() = default;

auto ShadowManager::OnFrameStart(RendererTag tag,
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  if (conventional_backend_) {
    conventional_backend_->OnFrameStart(tag, sequence, slot);
  }
}

auto ShadowManager::ResetCachedState() -> void
{
  last_view_directional_implementation_.clear();

  if (conventional_backend_) {
    conventional_backend_->ResetCachedState();
  }
}

auto ShadowManager::PublishForView(const ViewId view_id,
  const engine::ViewConstants& view_constants, const LightManager& lights,
  const float camera_viewport_width,
  const std::span<const glm::vec4> shadow_caster_bounds,
  const std::span<const glm::vec4> visible_receiver_bounds,
  const std::chrono::milliseconds gpu_budget,
  const std::uint64_t shadow_caster_content_hash) -> ShadowFramePublication
{
  static_cast<void>(camera_viewport_width);
  static_cast<void>(visible_receiver_bounds);
  static_cast<void>(gpu_budget);

  std::vector<engine::DirectionalShadowCandidate> candidates_storage;
  const auto light_candidates = lights.GetDirectionalShadowCandidates();
  candidates_storage.assign(light_candidates.begin(), light_candidates.end());

  for (const auto& candidate : light_candidates) {
    LOG_F(INFO,
      "ShadowManager: scene candidate view={} light_index={} flags=0x{:08x} "
      "mobility={} resolution_hint={} dir=({:.6f}, {:.6f}, {:.6f}) "
      "bias={:.6f} normal_bias={:.6f} sun_candidate={}",
      view_id.get(), candidate.light_index, candidate.light_flags,
      candidate.mobility, candidate.resolution_hint, candidate.direction_ws.x,
      candidate.direction_ws.y, candidate.direction_ws.z, candidate.bias,
      candidate.normal_bias, IsSceneSunShadowCandidate(candidate));
  }
  LOG_F(INFO, "ShadowManager: publish view={} scene_light_candidates={}",
    view_id.get(), light_candidates.size());

  if (!conventional_backend_) {
    LOG_F(WARNING,
      "ShadowManager: no conventional backend is available for view {}",
      view_id.get());
    RecordDirectionalImplementation(last_view_directional_implementation_,
      view_id, engine::ShadowImplementationKind::kNone,
      static_cast<std::uint32_t>(candidates_storage.size()));
    return {};
  }

  const auto publication
    = conventional_backend_->PublishView(view_id, view_constants,
      candidates_storage, shadow_caster_bounds, shadow_caster_content_hash);
  if (publication.shadow_instance_metadata_srv != kInvalidShaderVisibleIndex) {
    RecordDirectionalImplementation(last_view_directional_implementation_,
      view_id, engine::ShadowImplementationKind::kConventional,
      static_cast<std::uint32_t>(candidates_storage.size()));
  } else {
    RecordDirectionalImplementation(last_view_directional_implementation_,
      view_id, engine::ShadowImplementationKind::kNone,
      static_cast<std::uint32_t>(candidates_storage.size()));
  }
  return publication;
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

auto ShadowManager::TryGetShadowInstanceMetadata(
  const ViewId view_id) const noexcept -> const engine::ShadowInstanceMetadata*
{
  return conventional_backend_ != nullptr
    ? conventional_backend_->TryGetShadowInstanceMetadata(view_id)
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
