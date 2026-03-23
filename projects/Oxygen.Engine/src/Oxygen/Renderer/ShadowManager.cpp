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
      .basis_up_ws = oxygen::space::move::Up,
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

auto ShadowManager::ResetCachedState() -> void
{
  std::vector<ViewId> retired_view_ids;
  retired_view_ids.reserve(last_view_directional_implementation_.size()
    + virtual_gpu_raster_inputs_.size() + virtual_schedule_extractions_.size()
    + virtual_resolve_stats_extractions_.size());
  const auto record_retired_view = [&retired_view_ids](const ViewId view_id) {
    if (std::find(
          retired_view_ids.begin(), retired_view_ids.end(), view_id)
      == retired_view_ids.end()) {
      retired_view_ids.push_back(view_id);
    }
  };
  for (const auto& [view_id_value, _] : last_view_directional_implementation_) {
    record_retired_view(ViewId { view_id_value });
  }
  for (const auto& [view_id, _] : virtual_gpu_raster_inputs_) {
    record_retired_view(view_id);
  }
  for (const auto& [view_id, _] : virtual_schedule_extractions_) {
    record_retired_view(view_id);
  }
  for (const auto& [view_id, _] : virtual_resolve_stats_extractions_) {
    record_retired_view(view_id);
  }

  const auto previous_epoch = cache_epoch_;
  ++cache_epoch_;
  for (const auto view_id : retired_view_ids) {
    const auto previous_generation = GetOrCreateViewGeneration(view_id);
    ++virtual_view_generations_[view_id];
    LOG_F(INFO,
      "ShadowManager: retired virtual shadow view={} during cache reset "
      "cache_epoch={} -> {} view_generation {} -> {}",
      view_id.get(), previous_epoch, cache_epoch_, previous_generation,
      virtual_view_generations_[view_id]);
  }
  LOG_F(INFO,
    "ShadowManager: reset cached state cache_epoch {} -> {} "
    "(retired_views={} views_with_impl={} gpu_inputs={} schedule_extractions={} "
    "resolve_stats_extractions={})",
    previous_epoch, cache_epoch_, retired_view_ids.size(),
    last_view_directional_implementation_.size(),
    virtual_gpu_raster_inputs_.size(), virtual_schedule_extractions_.size(),
    virtual_resolve_stats_extractions_.size());
  last_view_directional_implementation_.clear();
  virtual_gpu_raster_inputs_.clear();
  virtual_frame_packets_.clear();
  virtual_schedule_extractions_.clear();
  virtual_resolve_stats_extractions_.clear();

  CHECK_F(virtual_frame_packets_.empty() && virtual_gpu_raster_inputs_.empty()
      && virtual_schedule_extractions_.empty()
      && virtual_resolve_stats_extractions_.empty(),
    "ShadowManager: scene reset failed to clear virtual shadow state");

  if (conventional_backend_) {
    conventional_backend_->ResetCachedState();
  }
  if (virtual_backend_) {
    virtual_backend_->ResetCachedState();
  }
}

auto ShadowManager::RetireView(const ViewId view_id) -> void
{
  const auto previous_generation = GetViewGeneration(view_id);
  last_view_directional_implementation_.erase(view_id.get());
  virtual_gpu_raster_inputs_.erase(view_id);
  virtual_frame_packets_.erase(view_id);
  virtual_schedule_extractions_.erase(view_id);
  virtual_resolve_stats_extractions_.erase(view_id);
  ++virtual_view_generations_[view_id];
  LOG_F(INFO,
    "ShadowManager: retired virtual shadow view={} cache_epoch={} "
    "view_generation {} -> {}",
    view_id.get(), cache_epoch_, previous_generation,
    virtual_view_generations_[view_id]);

  if (virtual_backend_) {
    virtual_backend_->RetireView(view_id);
  }
}

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
    for (auto& [view_id, extractions] : virtual_schedule_extractions_) {
      for (std::size_t extraction_index = 0U;
        extraction_index < extractions.size(); ++extraction_index) {
        auto& extraction = extractions[extraction_index];
        if (!extraction.pending_readback
          || extraction.mapped_readback_ptr == nullptr
          || extraction.source_frame_sequence.get() == 0U
          || extraction.source_frame_sequence.get() >= sequence.get()
          || extraction.last_consumed_sequence
            == extraction.source_frame_sequence) {
          continue;
        }
        const auto current_view_generation = GetViewGeneration(view_id);
        if (extraction.cache_epoch != cache_epoch_
          || extraction.view_generation != current_view_generation) {
          LOG_F(INFO,
            "ShadowManager: dropped stale virtual schedule feedback view={} "
            "slot={} source_frame={} extraction_epoch={} current_epoch={} "
            "extraction_view_generation={} current_view_generation={}",
            view_id.get(), extraction_index, extraction.source_frame_sequence.get(),
            extraction.cache_epoch, cache_epoch_, extraction.view_generation,
            current_view_generation);
          extraction.pending_readback = false;
          continue;
        }

        const auto extracted_scheduled_page_count
          = *extraction.mapped_readback_ptr;
        extraction.last_consumed_sequence = extraction.source_frame_sequence;
        extraction.pending_readback = false;
        LOG_F(INFO,
          "ShadowManager: consumed virtual schedule extraction view={} slot={} "
          "source_frame={} scheduled_pages={} cache_epoch={} "
          "view_generation={}",
          view_id.get(), extraction_index, extraction.source_frame_sequence.get(),
          extracted_scheduled_page_count, extraction.cache_epoch,
          extraction.view_generation);
        virtual_backend_->ApplyExtractedScheduleResult(view_id,
          extraction.source_frame_sequence, extraction.cache_epoch,
          extraction.view_generation, extracted_scheduled_page_count);
      }
    }
    for (auto& [view_id, extractions] : virtual_resolve_stats_extractions_) {
      for (std::size_t extraction_index = 0U;
        extraction_index < extractions.size(); ++extraction_index) {
        auto& extraction = extractions[extraction_index];
        if (!extraction.pending_readback
          || extraction.mapped_readback_ptr == nullptr
          || extraction.source_frame_sequence.get() == 0U
          || extraction.source_frame_sequence.get() >= sequence.get()
          || extraction.last_consumed_sequence
            == extraction.source_frame_sequence) {
          continue;
        }
        const auto current_view_generation = GetViewGeneration(view_id);
        if (extraction.cache_epoch != cache_epoch_
          || extraction.view_generation != current_view_generation) {
          LOG_F(INFO,
            "ShadowManager: dropped stale virtual resolve feedback view={} "
            "slot={} source_frame={} extraction_epoch={} current_epoch={} "
            "extraction_view_generation={} current_view_generation={}",
            view_id.get(), extraction_index, extraction.source_frame_sequence.get(),
            extraction.cache_epoch, cache_epoch_, extraction.view_generation,
            current_view_generation);
          extraction.pending_readback = false;
          continue;
        }

        const auto extracted_resolve_stats = *extraction.mapped_readback_ptr;
        extraction.last_consumed_sequence = extraction.source_frame_sequence;
        extraction.pending_readback = false;
        LOG_F(INFO,
          "ShadowManager: consumed virtual resolve extraction view={} slot={} "
          "source_frame={} scheduled={} rasterized={} requested={} "
          "requires_schedule={} cache_epoch={} view_generation={}",
          view_id.get(), extraction_index, extraction.source_frame_sequence.get(),
          extracted_resolve_stats.scheduled_raster_page_count,
          extracted_resolve_stats.rasterized_page_count,
          extracted_resolve_stats.requested_page_count,
          extracted_resolve_stats.pages_requiring_schedule_count,
          extraction.cache_epoch, extraction.view_generation);
        virtual_backend_->ApplyExtractedResolveStatsResult(
          view_id, extraction.source_frame_sequence, extraction.cache_epoch,
          extraction.view_generation, extracted_resolve_stats);
      }
    }
  }
}

auto ShadowManager::PublishForView(const ViewId view_id,
  const engine::ViewConstants& view_constants, const LightManager& lights,
  const float camera_viewport_width,
  const std::span<const glm::vec4> shadow_caster_bounds,
  const std::span<const glm::vec4> visible_receiver_bounds,
  const SyntheticSunShadowInput* synthetic_sun_shadow,
  const std::chrono::milliseconds gpu_budget,
  const std::uint64_t shadow_caster_content_hash) -> ShadowFramePublication
{
  static_cast<void>(GetOrCreateViewGeneration(view_id));
  std::vector<engine::DirectionalShadowCandidate> candidates_storage;
  const auto light_candidates = lights.GetDirectionalShadowCandidates();
  candidates_storage.assign(light_candidates.begin(), light_candidates.end());

  std::optional<engine::DirectionalShadowCandidate> synthetic_sun_candidate;
  if (synthetic_sun_shadow != nullptr && synthetic_sun_shadow->enabled) {
    synthetic_sun_candidate = BuildSyntheticSunCandidate(*synthetic_sun_shadow);
    static bool logged_synthetic_sun_candidate = false;
    if (!logged_synthetic_sun_candidate) {
      LOG_F(INFO,
        "ShadowManager synthetic sun candidate: bias={} normal_bias={} "
        "direction=({}, {}, {})",
        synthetic_sun_candidate->bias, synthetic_sun_candidate->normal_bias,
        synthetic_sun_candidate->direction_ws.x,
        synthetic_sun_candidate->direction_ws.y,
        synthetic_sun_candidate->direction_ws.z);
      logged_synthetic_sun_candidate = true;
    }
    candidates_storage.push_back(*synthetic_sun_candidate);
  }

  std::vector<engine::DirectionalShadowCandidate> virtual_candidates;
  const auto scene_sun_candidate_count
    = static_cast<std::size_t>(std::count_if(light_candidates.begin(),
      light_candidates.end(),
      [](const engine::DirectionalShadowCandidate& candidate) {
        return IsSceneSunShadowCandidate(candidate);
      }));
  const auto scene_non_sun_candidate_count
    = static_cast<std::size_t>(std::count_if(light_candidates.begin(),
      light_candidates.end(),
      [](const engine::DirectionalShadowCandidate& candidate) {
        return !IsSceneSunShadowCandidate(candidate);
      }));
  if (light_candidates.size() == 1U) {
    virtual_candidates.push_back(light_candidates.front());
  } else if (synthetic_sun_candidate.has_value()) {
    const bool has_scene_sun_candidates
      = std::any_of(light_candidates.begin(), light_candidates.end(),
        [](const engine::DirectionalShadowCandidate& candidate) {
          return IsSceneSunShadowCandidate(candidate);
        });
    const bool has_non_sun_scene_candidates
      = std::any_of(light_candidates.begin(), light_candidates.end(),
        [](const engine::DirectionalShadowCandidate& candidate) {
          return !IsSceneSunShadowCandidate(candidate);
        });
    if (!has_scene_sun_candidates && !has_non_sun_scene_candidates) {
      virtual_candidates.push_back(*synthetic_sun_candidate);
    }
  }
  const auto selected_virtual_light_index = !virtual_candidates.empty()
    ? virtual_candidates.front().light_index
    : 0xFFFFFFFFU;
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
  if (synthetic_sun_candidate.has_value()) {
    LOG_F(INFO,
      "ShadowManager: synthetic candidate view={} light_index={} "
      "flags=0x{:08x} resolution_hint={} dir=({:.6f}, {:.6f}, {:.6f}) "
      "bias={:.6f} normal_bias={:.6f}",
      view_id.get(), synthetic_sun_candidate->light_index,
      synthetic_sun_candidate->light_flags,
      synthetic_sun_candidate->resolution_hint,
      synthetic_sun_candidate->direction_ws.x,
      synthetic_sun_candidate->direction_ws.y,
      synthetic_sun_candidate->direction_ws.z, synthetic_sun_candidate->bias,
      synthetic_sun_candidate->normal_bias);
  }
  LOG_F(INFO,
    "ShadowManager: publish view={} scene_light_candidates={} "
    "scene_sun_candidates={} scene_non_sun_candidates={} "
    "synthetic_candidate={} selected_virtual_candidates={} "
    "selected_virtual_light_index={}",
    view_id.get(), light_candidates.size(), scene_sun_candidate_count,
    scene_non_sun_candidate_count, synthetic_sun_candidate.has_value(),
    virtual_candidates.size(), selected_virtual_light_index);

  const bool virtual_eligible = virtual_backend_ != nullptr
    && virtual_candidates.size() == 1U
    && directional_policy_
      != oxygen::DirectionalShadowImplementationPolicy::kConventionalOnly;

  if (virtual_eligible) {
    const auto publication
      = virtual_backend_->PublishView(view_id, view_constants,
        camera_viewport_width, virtual_candidates, shadow_caster_bounds,
        visible_receiver_bounds, gpu_budget, GetOrCreateViewGeneration(view_id),
        shadow_caster_content_hash);
    RefreshVirtualFramePacket(view_id);
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
    virtual_frame_packets_.erase(view_id);
    return {};
  }

  if (!conventional_backend_) {
    LOG_F(WARNING,
      "ShadowManager: no conventional backend is available for view {}",
      view_id.get());
    RecordDirectionalImplementation(last_view_directional_implementation_,
      view_id, engine::ShadowImplementationKind::kNone,
      static_cast<std::uint32_t>(candidates_storage.size()));
    virtual_frame_packets_.erase(view_id);
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
  virtual_frame_packets_.erase(view_id);
  return publication;
}

auto ShadowManager::ResolveVirtualCurrentFrame(const ViewId view_id) -> void
{
  if (virtual_backend_) {
    virtual_backend_->ResolveCurrentFrame(view_id);
    RefreshVirtualFramePacket(view_id);
  }
}

auto ShadowManager::PrepareVirtualPageTableResources(
  const ViewId view_id, graphics::CommandRecorder& recorder) -> void
{
  if (virtual_backend_) {
    virtual_backend_->PreparePageTableResources(view_id, recorder);
    RefreshVirtualFramePacket(view_id);
  }
}

auto ShadowManager::PrepareVirtualPageManagementOutputsForGpuWrite(
  const ViewId view_id, graphics::CommandRecorder& recorder) -> void
{
  if (virtual_backend_) {
    virtual_backend_->PreparePageManagementOutputsForGpuWrite(
      view_id, recorder);
    RefreshVirtualFramePacket(view_id);
  }
}

auto ShadowManager::FinalizeVirtualPageManagementOutputs(
  const ViewId view_id, graphics::CommandRecorder& recorder) -> void
{
  if (virtual_backend_) {
    virtual_backend_->FinalizePageManagementOutputs(view_id, recorder);
    RefreshVirtualFramePacket(view_id);
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
  inputs.cache_epoch = cache_epoch_;
  inputs.view_generation = GetOrCreateViewGeneration(view_id);
  LOG_F(INFO,
    "ShadowManager: submit gpu raster inputs view={} source_frame={} "
    "cache_epoch={} view_generation={} draw_count={} schedule_srv={} "
    "schedule_count_srv={} draw_ranges_srv={} draw_indices_srv={}",
    view_id.get(), inputs.source_frame_sequence.get(), inputs.cache_epoch,
    inputs.view_generation, inputs.draw_count, inputs.schedule_srv.get(),
    inputs.schedule_count_srv.get(), inputs.draw_page_ranges_srv.get(),
    inputs.draw_page_indices_srv.get());
  virtual_gpu_raster_inputs_.insert_or_assign(view_id, std::move(inputs));
  RefreshVirtualFramePacket(view_id);
}

auto ShadowManager::ClearVirtualGpuRasterInputs(const ViewId view_id) -> void
{
  if (const auto it = virtual_gpu_raster_inputs_.find(view_id);
    it != virtual_gpu_raster_inputs_.end()) {
    LOG_F(INFO,
      "ShadowManager: clear gpu raster inputs view={} source_frame={} "
      "cache_epoch={} view_generation={} draw_count={}",
      view_id.get(), it->second.source_frame_sequence.get(),
      it->second.cache_epoch, it->second.view_generation, it->second.draw_count);
  }
  virtual_gpu_raster_inputs_.erase(view_id);
  RefreshVirtualFramePacket(view_id);
}

auto ShadowManager::RegisterVirtualScheduleExtraction(const ViewId view_id,
  std::shared_ptr<graphics::Buffer> readback_buffer,
  std::uint32_t* mapped_readback_ptr,
  const frame::SequenceNumber source_sequence, const frame::Slot source_slot)
  -> void
{
  auto& extraction
    = virtual_schedule_extractions_[view_id][static_cast<std::size_t>(
      source_slot.get())];
  extraction.readback_buffer = std::move(readback_buffer);
  extraction.mapped_readback_ptr = mapped_readback_ptr;
  extraction.source_frame_sequence = source_sequence;
  extraction.cache_epoch = cache_epoch_;
  extraction.view_generation = GetOrCreateViewGeneration(view_id);
  extraction.pending_readback = mapped_readback_ptr != nullptr;
  LOG_F(INFO,
    "ShadowManager: registered virtual schedule extraction view={} slot={} "
    "source_frame={} cache_epoch={} view_generation={} pending_readback={}",
    view_id.get(), source_slot.get(), source_sequence.get(), extraction.cache_epoch,
    extraction.view_generation, extraction.pending_readback);
}

auto ShadowManager::RegisterVirtualResolveStatsExtraction(const ViewId view_id,
  std::shared_ptr<graphics::Buffer> readback_buffer,
  renderer::VirtualShadowResolveStats* mapped_readback_ptr,
  const frame::SequenceNumber source_sequence, const frame::Slot source_slot)
  -> void
{
  auto& extraction
    = virtual_resolve_stats_extractions_[view_id][static_cast<std::size_t>(
      source_slot.get())];
  extraction.readback_buffer = std::move(readback_buffer);
  extraction.mapped_readback_ptr = mapped_readback_ptr;
  extraction.source_frame_sequence = source_sequence;
  extraction.cache_epoch = cache_epoch_;
  extraction.view_generation = GetOrCreateViewGeneration(view_id);
  extraction.pending_readback = mapped_readback_ptr != nullptr;
  LOG_F(INFO,
    "ShadowManager: registered virtual resolve extraction view={} slot={} "
    "source_frame={} cache_epoch={} view_generation={} pending_readback={}",
    view_id.get(), source_slot.get(), source_sequence.get(), extraction.cache_epoch,
    extraction.view_generation, extraction.pending_readback);
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
  if (it == virtual_gpu_raster_inputs_.end()) {
    return nullptr;
  }
  if (it->second.cache_epoch != cache_epoch_
    || it->second.view_generation != GetViewGeneration(view_id)) {
    return nullptr;
  }
  return &it->second;
}

auto ShadowManager::TryGetVirtualFramePacket(
  const ViewId view_id) const noexcept
  -> const renderer::VirtualShadowFramePacket*
{
  const auto it = virtual_frame_packets_.find(view_id);
  if (it == virtual_frame_packets_.end()) {
    return nullptr;
  }
  if (it->second.cache_epoch != cache_epoch_
    || it->second.view_generation != GetViewGeneration(view_id)) {
    return nullptr;
  }
  return &it->second;
}

auto ShadowManager::TryGetVirtualPageManagementBindings(
  const ViewId view_id) const noexcept
  -> const VirtualShadowPageManagementBindings*
{
  return virtual_backend_
    ? virtual_backend_->TryGetPageManagementBindings(view_id)
    : nullptr;
}

auto ShadowManager::TryGetVirtualPageManagementStateSnapshot(
  const ViewId view_id) const noexcept
  -> std::optional<VirtualShadowPageManagementStateSnapshot>
{
  return virtual_backend_
    ? virtual_backend_->TryGetPageManagementStateSnapshot(view_id)
    : std::nullopt;
}

auto ShadowManager::TryGetVirtualDirectionalMetadata(
  const ViewId view_id) const noexcept
  -> const engine::DirectionalVirtualShadowMetadata*
{
  return virtual_backend_
    ? virtual_backend_->TryGetDirectionalVirtualMetadata(view_id)
    : nullptr;
}

auto ShadowManager::TryGetShadowInstanceMetadata(const ViewId view_id)
  const noexcept -> const engine::ShadowInstanceMetadata*
{
  if (virtual_backend_ != nullptr) {
    if (const auto* instance
      = virtual_backend_->TryGetShadowInstanceMetadata(view_id);
      instance != nullptr) {
      return instance;
    }
  }
  return conventional_backend_ != nullptr
    ? conventional_backend_->TryGetShadowInstanceMetadata(view_id)
    : nullptr;
}

auto ShadowManager::TryGetVirtualPageFlagsBuffer(
  const ViewId view_id) const noexcept -> std::shared_ptr<graphics::Buffer>
{
  return virtual_backend_ ? virtual_backend_->TryGetPageFlagsBuffer(view_id)
                          : nullptr;
}

auto ShadowManager::TryGetVirtualPhysicalPageMetadataBuffer(
  const ViewId view_id) const noexcept -> std::shared_ptr<graphics::Buffer>
{
  return virtual_backend_
    ? virtual_backend_->TryGetPhysicalPageMetadataBuffer(view_id)
    : nullptr;
}

auto ShadowManager::TryGetVirtualResolveStatsBuffer(
  const ViewId view_id) const noexcept -> std::shared_ptr<graphics::Buffer>
{
  return virtual_backend_ ? virtual_backend_->TryGetResolveStatsBuffer(view_id)
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

auto ShadowManager::GetOrCreateViewGeneration(const ViewId view_id)
  -> std::uint64_t
{
  auto [it, inserted] = virtual_view_generations_.try_emplace(view_id, 1U);
  if (inserted && it->second == 0U) {
    it->second = 1U;
  }
  return it->second;
}

auto ShadowManager::GetViewGeneration(const ViewId view_id) const
  -> std::uint64_t
{
  if (const auto it = virtual_view_generations_.find(view_id);
    it != virtual_view_generations_.end()) {
    return it->second;
  }
  return 1U;
}

auto ShadowManager::RefreshVirtualFramePacket(const ViewId view_id) -> void
{
  if (!virtual_backend_) {
    virtual_frame_packets_.erase(view_id);
    return;
  }

  renderer::VirtualShadowFramePacket packet {};
  packet.cache_epoch = cache_epoch_;
  packet.view_generation = GetViewGeneration(view_id);

  if (const auto* publication = virtual_backend_->TryGetFramePublication(view_id);
    publication != nullptr) {
    packet.publication = *publication;
    packet.virtual_directional_shadow_metadata_srv
      = publication->virtual_directional_shadow_metadata_srv;
  }

  if (const auto* metadata = virtual_backend_->TryGetDirectionalVirtualMetadata(view_id);
    metadata != nullptr) {
    packet.directional_metadata = *metadata;
    packet.has_directional_metadata = true;
  }

  if (const auto* bindings = virtual_backend_->TryGetPageManagementBindings(view_id);
    bindings != nullptr) {
    packet.page_management_bindings = *bindings;
    packet.has_page_management_bindings = true;
  }

  if (const auto state = virtual_backend_->TryGetPageManagementStateSnapshot(view_id);
    state.has_value()) {
    packet.page_management_state = *state;
    packet.has_page_management_state = true;
  }

  packet.page_flags_buffer = virtual_backend_->TryGetPageFlagsBuffer(view_id);
  packet.physical_page_metadata_buffer
    = virtual_backend_->TryGetPhysicalPageMetadataBuffer(view_id);
  packet.resolve_stats_buffer = virtual_backend_->TryGetResolveStatsBuffer(view_id);

  if (const auto it = virtual_gpu_raster_inputs_.find(view_id);
    it != virtual_gpu_raster_inputs_.end()
    && it->second.cache_epoch == cache_epoch_
    && it->second.view_generation == packet.view_generation) {
    packet.gpu_raster_inputs = it->second;
    packet.has_gpu_raster_inputs = true;
    packet.source_frame_sequence = it->second.source_frame_sequence;
  }

  const bool has_live_virtual_state
    = packet.virtual_directional_shadow_metadata_srv.IsValid()
    || packet.has_directional_metadata || packet.has_page_management_bindings
    || packet.has_gpu_raster_inputs || packet.page_flags_buffer != nullptr
    || packet.physical_page_metadata_buffer != nullptr
    || packet.resolve_stats_buffer != nullptr;
  if (!has_live_virtual_state) {
    virtual_frame_packets_.erase(view_id);
    return;
  }

  LOG_F(INFO,
    "ShadowManager: vsm packet refresh view={} source_frame={} cache_epoch={} "
    "view_generation={} meta_srv={} page_table_srv={} page_flags_srv={} "
    "pool_srv={} phys_meta_srv={} phys_lists_srv={} schedule_srv={} "
    "schedule_count_srv={} has_metadata={} has_page_management={} "
    "has_gpu_raster_inputs={} reset_state={} reset_request_pending={}",
    view_id.get(), packet.source_frame_sequence.get(), packet.cache_epoch,
    packet.view_generation, packet.virtual_directional_shadow_metadata_srv.get(),
    packet.publication.virtual_shadow_page_table_srv.get(),
    packet.publication.virtual_shadow_page_flags_srv.get(),
    packet.publication.virtual_shadow_physical_pool_srv.get(),
    packet.publication.virtual_shadow_physical_page_metadata_srv.get(),
    packet.publication.virtual_shadow_physical_page_lists_srv.get(),
    packet.has_gpu_raster_inputs ? packet.gpu_raster_inputs.schedule_srv.get()
                                 : kInvalidShaderVisibleIndex.get(),
    packet.has_gpu_raster_inputs
      ? packet.gpu_raster_inputs.schedule_count_srv.get()
      : kInvalidShaderVisibleIndex.get(),
    packet.has_directional_metadata, packet.has_page_management_bindings,
    packet.has_gpu_raster_inputs,
    packet.has_page_management_state
      ? packet.page_management_state.reset_page_management_state
      : 0U,
    packet.has_page_management_state
      ? packet.page_management_state.reset_request_pending
      : 0U);

  virtual_frame_packets_.insert_or_assign(view_id, std::move(packet));
}

} // namespace oxygen::renderer
