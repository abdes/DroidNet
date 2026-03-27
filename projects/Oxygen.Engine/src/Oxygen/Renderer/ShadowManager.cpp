//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>
#include <optional>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/Internal/ConventionalShadowBackend.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.h>

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
    case engine::ShadowImplementationKind::kVirtual:
      LOG_F(INFO,
        "ShadowManager: view {} is using virtual directional shadows "
        "(eligible_candidates={})",
        key, candidate_count);
      break;
    default:
      LOG_F(INFO,
        "ShadowManager: view {} has no published directional shadow products "
        "(eligible_candidates={})",
        key, candidate_count);
      break;
    }
  }

  [[nodiscard]] auto BuildShadowProductFlags(const std::uint32_t light_flags)
    -> std::uint32_t
  {
    using engine::DirectionalLightFlags;
    using engine::ShadowProductFlags;

    auto flags = ShadowProductFlags::kValid;
    const auto directional_flags
      = static_cast<DirectionalLightFlags>(light_flags);

    if ((directional_flags & DirectionalLightFlags::kContactShadows)
      != DirectionalLightFlags::kNone) {
      flags |= ShadowProductFlags::kContactShadows;
    }
    if ((directional_flags & DirectionalLightFlags::kSunLight)
      != DirectionalLightFlags::kNone) {
      flags |= ShadowProductFlags::kSunLight;
    }

    return static_cast<std::uint32_t>(flags);
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
  , vsm_shadow_instance_buffer_(gfx_, *staging_provider_,
      static_cast<std::uint32_t>(sizeof(engine::ShadowInstanceMetadata)),
      inline_transfers_, "ShadowManager.VsmShadowInstanceMetadata")
  , conventional_backend_(std::make_unique<internal::ConventionalShadowBackend>(
      gfx_, staging_provider_, inline_transfers_, shadow_quality_tier_))
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "expecting valid staging provider");
  DCHECK_NOTNULL_F(inline_transfers_, "expecting valid transfer coordinator");
  if (directional_policy_
    == oxygen::DirectionalShadowImplementationPolicy::kVirtualShadowMap) {
    vsm_shadow_renderer_ = std::make_unique<vsm::VsmShadowRenderer>(
      gfx_, staging_provider_, inline_transfers_, shadow_quality_tier_);
  }

  LOG_F(INFO,
    "ShadowManager: initialized directional policy={} conventional_backend={} "
    "vsm_shell={}",
    directional_policy_,
    conventional_backend_ != nullptr ? "enabled" : "disabled",
    vsm_shadow_renderer_ != nullptr ? "enabled" : "disabled");
  if (directional_policy_
    == oxygen::DirectionalShadowImplementationPolicy::kVirtualShadowMap) {
    LOG_F(INFO,
      "ShadowManager: VSM policy selected; directional publication will use "
      "the VSM lighting product and skip conventional directional raster "
      "plans");
  }
}

ShadowManager::~ShadowManager() = default;

auto ShadowManager::OnFrameStart(RendererTag tag,
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  vsm_shadow_instance_buffer_.OnFrameStart(sequence, slot);
  virtual_view_cache_.clear();
  if (conventional_backend_) {
    conventional_backend_->OnFrameStart(tag, sequence, slot);
  }
  if (vsm_shadow_renderer_) {
    vsm_shadow_renderer_->OnFrameStart(tag, sequence, slot);
  }
}

auto ShadowManager::ResetCachedState() -> void
{
  last_view_directional_implementation_.clear();
  virtual_view_cache_.clear();

  if (conventional_backend_) {
    conventional_backend_->ResetCachedState();
  }
  if (vsm_shadow_renderer_) {
    vsm_shadow_renderer_->ResetCachedState();
  }
}

auto ShadowManager::PublishForView(const ViewId view_id,
  const engine::ViewConstants& view_constants, const LightManager& lights,
  const observer_ptr<scene::Scene> active_scene,
  const float camera_viewport_width,
  const std::span<const engine::sceneprep::RenderItemData> rendered_items,
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

  if (vsm_shadow_renderer_) {
#if !defined(NDEBUG)
    CHECK_F(view_constants.GetBindlessViewFrameBindingsSlot().IsValid(),
      "ShadowManager: VSM preparation requires a valid bindless "
      "view-frame bindings slot for view {}",
      view_id.get());
#endif // !defined(NDEBUG)
    const auto has_virtual_shadow_work = vsm_shadow_renderer_->PrepareView(
      view_id, view_constants, lights, active_scene, camera_viewport_width,
      rendered_items, shadow_caster_bounds, visible_receiver_bounds, gpu_budget,
      shadow_caster_content_hash);
    if (has_virtual_shadow_work) {
      LOG_F(INFO,
        "ShadowManager: prepared VSM shell inputs for view {} "
        "(directional_policy={})",
        view_id.get(), static_cast<std::uint32_t>(directional_policy_));
    }
  }

  if (directional_policy_
    == oxygen::DirectionalShadowImplementationPolicy::kVirtualShadowMap) {
    auto state = VirtualViewCacheEntry {};
    state.shadow_instances.reserve(candidates_storage.size());

    std::optional<std::size_t> primary_virtual_candidate_index {};
    if (!candidates_storage.empty()) {
      primary_virtual_candidate_index = std::size_t { 0U };
      if (candidates_storage.size() > 1U) {
        LOG_F(WARNING,
          "ShadowManager: VSM directional publication currently routes the "
          "primary directional candidate only (view={} candidate_count={} "
          "primary_light_index={})",
          view_id.get(), candidates_storage.size(),
          candidates_storage.front().light_index);
      }
    }

    for (std::size_t candidate_index = 0U;
      candidate_index < candidates_storage.size(); ++candidate_index) {
      const auto& candidate = candidates_storage[candidate_index];
      const auto is_primary_virtual_candidate
        = primary_virtual_candidate_index.has_value()
        && *primary_virtual_candidate_index == candidate_index;
      state.shadow_instances.push_back(engine::ShadowInstanceMetadata {
        .light_index = candidate.light_index,
        .payload_index = 0U,
        .domain = static_cast<std::uint32_t>(engine::ShadowDomain::kDirectional),
        .implementation_kind = static_cast<std::uint32_t>(
          is_primary_virtual_candidate
            ? engine::ShadowImplementationKind::kVirtual
            : engine::ShadowImplementationKind::kNone),
        .flags = is_primary_virtual_candidate
          ? BuildShadowProductFlags(candidate.light_flags)
          : 0U,
      });
      if (is_primary_virtual_candidate
        && (state.shadow_instances.back().flags
             & static_cast<std::uint32_t>(engine::ShadowProductFlags::kSunLight))
          != 0U) {
        state.frame_publication.sun_shadow_index
          = static_cast<std::uint32_t>(candidate_index);
      }
    }

    if (!state.shadow_instances.empty()) {
      const auto allocation = vsm_shadow_instance_buffer_.Allocate(
        static_cast<std::uint32_t>(state.shadow_instances.size()));
      if (!allocation) {
        LOG_F(ERROR,
          "ShadowManager: failed to allocate VSM shadow-instance metadata "
          "buffer for view {}: {}",
          view_id.get(), allocation.error().message());
      } else {
        const auto& published = *allocation;
        if (published.mapped_ptr != nullptr) {
          std::memcpy(published.mapped_ptr, state.shadow_instances.data(),
            state.shadow_instances.size()
              * sizeof(engine::ShadowInstanceMetadata));
        }
        state.frame_publication.shadow_instance_metadata_srv = published.srv;
      }
    }

    if (state.frame_publication.shadow_instance_metadata_srv
      != kInvalidShaderVisibleIndex) {
      RecordDirectionalImplementation(last_view_directional_implementation_,
        view_id, engine::ShadowImplementationKind::kVirtual,
        static_cast<std::uint32_t>(candidates_storage.size()));
    } else {
      RecordDirectionalImplementation(last_view_directional_implementation_,
        view_id, engine::ShadowImplementationKind::kNone,
        static_cast<std::uint32_t>(candidates_storage.size()));
    }

    auto [it, inserted]
      = virtual_view_cache_.insert_or_assign(view_id, std::move(state));
    DCHECK_F(inserted || it != virtual_view_cache_.end(),
      "ShadowManager failed to cache VSM publication for view {}",
      view_id.get());
    return it->second.frame_publication;
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
  if (directional_policy_
    == oxygen::DirectionalShadowImplementationPolicy::kVirtualShadowMap) {
    const auto it = virtual_view_cache_.find(view_id);
    return it != virtual_view_cache_.end() ? &it->second.frame_publication
                                           : nullptr;
  }
  return conventional_backend_
    ? conventional_backend_->TryGetFramePublication(view_id)
    : nullptr;
}

auto ShadowManager::TryGetRasterRenderPlan(const ViewId view_id) const noexcept
  -> const RasterShadowRenderPlan*
{
  if (directional_policy_
    == oxygen::DirectionalShadowImplementationPolicy::kVirtualShadowMap) {
    static_cast<void>(view_id);
    return nullptr;
  }
  return conventional_backend_
    ? conventional_backend_->TryGetRasterRenderPlan(view_id)
    : nullptr;
}

auto ShadowManager::TryGetShadowInstanceMetadata(
  const ViewId view_id) const noexcept -> const engine::ShadowInstanceMetadata*
{
  if (directional_policy_
    == oxygen::DirectionalShadowImplementationPolicy::kVirtualShadowMap) {
    const auto it = virtual_view_cache_.find(view_id);
    return it != virtual_view_cache_.end() && !it->second.shadow_instances.empty()
      ? &it->second.shadow_instances.front()
      : nullptr;
  }
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

auto ShadowManager::GetVirtualShadowRenderer() const noexcept
  -> observer_ptr<vsm::VsmShadowRenderer>
{
  return observer_ptr { vsm_shadow_renderer_.get() };
}

} // namespace oxygen::renderer
