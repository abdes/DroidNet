//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <optional>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/Internal/ConventionalShadowBackend.h>
#include <Oxygen/Renderer/Internal/VirtualShadowMapBackend.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Scene/Light/LightCommon.h>

namespace oxygen::renderer {

namespace {

  auto BuildSyntheticSunCandidate(
    const ShadowManager::SyntheticSunShadowInput& synthetic_sun_shadow)
    -> engine::DirectionalShadowCandidate
  {
    return engine::DirectionalShadowCandidate {
      .light_index = 0xFFFFFFFFU,
      .light_flags
      = static_cast<std::uint32_t>(engine::DirectionalLightFlags::kAffectsWorld
        | engine::DirectionalLightFlags::kCastsShadows
        | engine::DirectionalLightFlags::kSunLight
        | engine::DirectionalLightFlags::kEnvironmentContribution),
      .mobility = static_cast<std::uint32_t>(scene::LightMobility::kRealtime),
      .resolution_hint = synthetic_sun_shadow.resolution_hint,
      .direction_ws = synthetic_sun_shadow.direction_ws,
      .bias = synthetic_sun_shadow.bias,
      .normal_bias = synthetic_sun_shadow.normal_bias,
      .cascade_count = synthetic_sun_shadow.cascade_count,
      .distribution_exponent = synthetic_sun_shadow.distribution_exponent,
      .cascade_distances = synthetic_sun_shadow.cascade_distances,
    };
  }

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
    case engine::ShadowImplementationKind::kVirtual:
      LOG_F(INFO,
        "ShadowManager: view {} is using virtual directional shadows "
        "(eligible_candidates={})",
        key, candidate_count);
      break;
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
  , directional_policy_(directional_policy)
  , conventional_backend_(std::make_unique<internal::ConventionalShadowBackend>(
      gfx_, staging_provider_, inline_transfers_, shadow_quality_tier_))
  , virtual_backend_(
      std::make_unique<internal::VirtualShadowMapBackend>(gfx_.get(),
        staging_provider_.get(), inline_transfers_.get(), shadow_quality_tier_))
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "expecting valid staging provider");
  DCHECK_NOTNULL_F(inline_transfers_, "expecting valid transfer coordinator");
  if (virtual_backend_) {
    virtual_backend_->SetDirectionalBiasSettings(
      directional_virtual_bias_settings_);
  }
}

ShadowManager::~ShadowManager() = default;

auto ShadowManager::SetDirectionalVirtualBiasSettings(
  const DirectionalVirtualBiasSettings& settings) noexcept -> void
{
  directional_virtual_bias_settings_ = settings;
  if (virtual_backend_) {
    virtual_backend_->SetDirectionalBiasSettings(
      directional_virtual_bias_settings_);
  }
}

auto ShadowManager::OnFrameStart(RendererTag tag,
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  if (conventional_backend_) {
    conventional_backend_->OnFrameStart(tag, sequence, slot);
  }
  if (virtual_backend_) {
    virtual_backend_->OnFrameStart(tag, sequence, slot);
  }
}

auto ShadowManager::PublishForView(const ViewId view_id,
  const engine::ViewConstants& view_constants, const LightManager& lights,
  const std::span<const glm::vec4> shadow_caster_bounds,
  const std::span<const glm::vec4> visible_receiver_bounds,
  const SyntheticSunShadowInput* synthetic_sun_shadow,
  const std::chrono::milliseconds gpu_budget,
  const std::uint64_t shadow_caster_content_hash) -> ShadowFramePublication
{
  std::vector<engine::DirectionalShadowCandidate> candidates_storage;
  const auto light_candidates = lights.GetDirectionalShadowCandidates();
  candidates_storage.assign(light_candidates.begin(), light_candidates.end());

  std::optional<engine::DirectionalShadowCandidate> synthetic_sun_candidate;
  if (synthetic_sun_shadow != nullptr && synthetic_sun_shadow->enabled) {
    synthetic_sun_candidate = BuildSyntheticSunCandidate(*synthetic_sun_shadow);
    static bool logged_synthetic_sun_candidate = false;
    if (!logged_synthetic_sun_candidate) {
      LOG_F(INFO,
        "ShadowManager synthetic sun candidate: bias={} normal_bias={} direction=({}, {}, {})",
        synthetic_sun_candidate->bias,
        synthetic_sun_candidate->normal_bias,
        synthetic_sun_candidate->direction_ws.x,
        synthetic_sun_candidate->direction_ws.y,
        synthetic_sun_candidate->direction_ws.z);
      logged_synthetic_sun_candidate = true;
    }
    candidates_storage.push_back(*synthetic_sun_candidate);
  }

  std::vector<engine::DirectionalShadowCandidate> virtual_candidates;
  if (synthetic_sun_candidate.has_value()) {
    const bool has_non_sun_scene_candidates
      = std::any_of(light_candidates.begin(), light_candidates.end(),
        [](const engine::DirectionalShadowCandidate& candidate) {
          return !IsSceneSunShadowCandidate(candidate);
        });
    if (!has_non_sun_scene_candidates) {
      // The current directional VSM slice can publish exactly one shadowed
      // directional source. When the resolved sun is synthetic, the sun path
      // owns that slot and any scene-backed sun light is skipped by shading.
      virtual_candidates.push_back(*synthetic_sun_candidate);
    }
  } else if (light_candidates.size() == 1U) {
    virtual_candidates.push_back(light_candidates.front());
  }

  const bool virtual_eligible = virtual_backend_ != nullptr
    && virtual_candidates.size() == 1U
    && directional_policy_
      != oxygen::DirectionalShadowImplementationPolicy::kConventionalOnly;

  if (virtual_eligible) {
    const auto publication = virtual_backend_->PublishView(view_id,
      view_constants, virtual_candidates, shadow_caster_bounds,
      visible_receiver_bounds, gpu_budget, shadow_caster_content_hash);
    if (publication.shadow_instance_metadata_srv
      != kInvalidShaderVisibleIndex) {
      RecordDirectionalImplementation(last_view_directional_implementation_,
        view_id, engine::ShadowImplementationKind::kVirtual,
        static_cast<std::uint32_t>(virtual_candidates.size()));
      return publication;
    }
    if (directional_policy_
      == oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly) {
      LOG_F(WARNING,
        "ShadowManager: virtual-only directional policy could not publish "
        "virtual directional shadows for view {}",
        view_id.get());
      RecordDirectionalImplementation(last_view_directional_implementation_,
        view_id, engine::ShadowImplementationKind::kNone,
        static_cast<std::uint32_t>(virtual_candidates.size()));
      return {};
    }
  } else if (directional_policy_
    == oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly) {
    if (!candidates_storage.empty()) {
      LOG_F(WARNING,
        "ShadowManager: virtual-only directional policy could not activate "
        "for view {} (eligible_candidates={})",
        view_id.get(), virtual_candidates.size());
    }
    RecordDirectionalImplementation(last_view_directional_implementation_,
      view_id, engine::ShadowImplementationKind::kNone,
      static_cast<std::uint32_t>(virtual_candidates.size()));
    return {};
  }

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

auto ShadowManager::MarkVirtualRasterExecuted(
  const ViewId view_id, const bool rendered_page_work) -> void
{
  if (virtual_backend_) {
    virtual_backend_->MarkRendered(view_id, rendered_page_work);
  }
}

auto ShadowManager::ResolveVirtualCurrentFrame(const ViewId view_id) -> void
{
  if (virtual_backend_) {
    virtual_backend_->ResolveCurrentFrame(view_id);
  }
}

auto ShadowManager::PrepareVirtualPageTableResources(
  const ViewId view_id, graphics::CommandRecorder& recorder) -> void
{
  if (virtual_backend_) {
    virtual_backend_->PreparePageTableResources(view_id, recorder);
  }
}

auto ShadowManager::PrepareVirtualPageManagementOutputsForGpuWrite(
  const ViewId view_id, graphics::CommandRecorder& recorder) -> void
{
  if (virtual_backend_) {
    virtual_backend_->PreparePageManagementOutputsForGpuWrite(
      view_id, recorder);
  }
}

auto ShadowManager::FinalizeVirtualPageManagementOutputs(
  const ViewId view_id, graphics::CommandRecorder& recorder) -> void
{
  if (virtual_backend_) {
    virtual_backend_->FinalizePageManagementOutputs(view_id, recorder);
  }
}

auto ShadowManager::SetPublishedViewFrameBindingsSlot(const ViewId view_id,
  const engine::BindlessViewFrameBindingsSlot slot) -> void
{
  if (conventional_backend_) {
    conventional_backend_->SetPublishedViewFrameBindingsSlot(view_id, slot);
  }
  if (virtual_backend_) {
    virtual_backend_->SetPublishedViewFrameBindingsSlot(view_id, slot);
  }
}

auto ShadowManager::SubmitVirtualGpuRasterInputs(
  const ViewId view_id, renderer::VirtualShadowGpuRasterInputs inputs) -> void
{
  virtual_gpu_raster_inputs_.insert_or_assign(view_id, std::move(inputs));
}

auto ShadowManager::ClearVirtualGpuRasterInputs(const ViewId view_id) -> void
{
  virtual_gpu_raster_inputs_.erase(view_id);
}

auto ShadowManager::TryGetFramePublication(const ViewId view_id) const noexcept
  -> const ShadowFramePublication*
{
  if (virtual_backend_) {
    if (const auto* publication
      = virtual_backend_->TryGetFramePublication(view_id);
      publication != nullptr) {
      return publication;
    }
  }
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

auto ShadowManager::TryGetVirtualGpuRasterInputs(
  const ViewId view_id) const noexcept
  -> const renderer::VirtualShadowGpuRasterInputs*
{
  const auto it = virtual_gpu_raster_inputs_.find(view_id);
  return it != virtual_gpu_raster_inputs_.end() ? &it->second : nullptr;
}

auto ShadowManager::TryGetVirtualPageManagementBindings(
  const ViewId view_id) const noexcept
  -> const VirtualShadowPageManagementBindings*
{
  return virtual_backend_
    ? virtual_backend_->TryGetPageManagementBindings(view_id)
    : nullptr;
}

auto ShadowManager::TryGetVirtualDirectionalMetadata(
  const ViewId view_id) const noexcept
  -> const engine::DirectionalVirtualShadowMetadata*
{
  return virtual_backend_
    ? virtual_backend_->TryGetDirectionalVirtualMetadata(view_id)
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

auto ShadowManager::GetVirtualShadowDepthTexture() const noexcept
  -> const std::shared_ptr<graphics::Texture>&
{
  if (virtual_backend_) {
    return virtual_backend_->GetPhysicalPoolTexture();
  }

  static const std::shared_ptr<graphics::Texture> kNullTexture {};
  return kNullTexture;
}

} // namespace oxygen::renderer
