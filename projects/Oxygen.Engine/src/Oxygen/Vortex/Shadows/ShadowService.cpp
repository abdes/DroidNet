//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/AllocationBudget.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Profiling/CpuProfileScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Vortex/Lighting/Types/LightingPreparationFailure.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Shadows/Internal/ShadowCasterDependencies.h>
#include <Oxygen/Vortex/Shadows/Internal/ShadowEligibility.h>
#include <Oxygen/Vortex/Shadows/Internal/ShadowReferenceBuilder.h>
#include <Oxygen/Vortex/Shadows/Internal/SharedShadowMap.h>
#include <Oxygen/Vortex/Shadows/Passes/CascadeShadowPass.h>
#include <Oxygen/Vortex/Shadows/Passes/ContactShadowCasterDepthPass.h>
#include <Oxygen/Vortex/Shadows/ShadowService.h>
#include <Oxygen/Vortex/Shadows/Types/CubeLocalShadowRecord.h>
#include <Oxygen/Vortex/Shadows/Types/DirectionalShadowRecord.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/Shadows/Types/LightShadowReference.h>
#include <Oxygen/Vortex/Shadows/Types/ProjectedLocalShadowRecord.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowCascadeBinding.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowFrameData.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/LightingFrameBindings.h>
#include <Oxygen/Vortex/Types/ShadowFrameBindings.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>

namespace oxygen::vortex {

ShadowService::ShadowService(Renderer& renderer)
  : renderer_(renderer)
  , cascade_shadow_pass_(std::make_unique<shadows::CascadeShadowPass>(renderer))
  , contact_depth_pass_(
      std::make_unique<shadows::ContactShadowCasterDepthPass>(renderer))
{
}

ShadowService::~ShadowService() { CloseFramePublications(); }
auto ShadowService::CloseFramePublications() noexcept -> void
{
  for (auto& [view, publication] : published_views_) {
    if (publication.read_set) {
      publication.read_set->Close();
    }
  }
}

auto ShadowService::EnsurePublishResources() -> bool
{
  if (bindings_publisher_ != nullptr) {
    return true;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return false;
  }

  bindings_publisher_ = std::make_unique<
    internal::PerViewStructuredPublisher<ShadowFrameBindings>>(
    observer_ptr { gfx.get() }, renderer_.GetLightingStagingProvider(),
    observer_ptr { &renderer_.GetInlineTransfersCoordinator() },
    "ShadowFrameBindings");
  const auto make_buffer =
    [&](const std::uint32_t stride,
      const char* label) -> std::unique_ptr<upload::TransientStructuredBuffer> {
    return std::make_unique<upload::TransientStructuredBuffer>(
      observer_ptr { gfx.get() }, renderer_.GetLightingStagingProvider(),
      stride, observer_ptr { &renderer_.GetInlineTransfersCoordinator() },
      label);
  };
  directional_record_buffer_ = make_buffer(
    sizeof(DirectionalShadowRecord), "ShadowService.DirectionalRecords");
  cascade_record_buffer_
    = make_buffer(sizeof(ShadowCascadeBinding), "ShadowService.CascadeRecords");
  projected_record_buffer_ = make_buffer(
    sizeof(ProjectedLocalShadowRecord), "ShadowService.ProjectedRecords");
  cube_record_buffer_
    = make_buffer(sizeof(CubeLocalShadowRecord), "ShadowService.CubeRecords");
  directional_reference_buffer_ = make_buffer(
    sizeof(LightShadowReference), "ShadowService.DirectionalReferences");
  local_reference_buffer_ = make_buffer(
    sizeof(LightShadowReference), "ShadowService.LocalReferences");
  return true;
}

auto ShadowService::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  std::erase_if(prepared_casters_, [&](const auto& entry) {
    return entry.second.last_seen != current_sequence_;
  });
  caster_records_.Prune();
  current_sequence_ = sequence;
  current_slot_ = slot;
  CloseFramePublications();
  published_views_.clear();
  failed_views_.clear();
  last_render_state_ = {
    .frame_sequence = sequence,
    .frame_slot = slot,
  };
  cascade_shadow_pass_->OnFrameStart(sequence, slot);
  contact_depth_pass_->OnFrameStart(sequence);
  if (EnsurePublishResources()) {
    bindings_publisher_->OnFrameStart(sequence, slot);
    directional_record_buffer_->OnFrameStart(sequence, slot);
    cascade_record_buffer_->OnFrameStart(sequence, slot);
    projected_record_buffer_->OnFrameStart(sequence, slot);
    cube_record_buffer_->OnFrameStart(sequence, slot);
    directional_reference_buffer_->OnFrameStart(sequence, slot);
    local_reference_buffer_->OnFrameStart(sequence, slot);
  }
}

auto ShadowService::PublishShadowBindings(
  const ViewId view_id, ShadowFrameData& data) -> ShaderVisibleIndex
{
  if (!EnsurePublishResources()) {
    return kInvalidShaderVisibleIndex;
  }
  const auto write
    = []<typename T>(upload::TransientStructuredBuffer& buffer,
        const std::vector<T>& records, ShaderVisibleIndex& descriptor,
        std::uint32_t& count) -> bool {
    if (records.size() > std::numeric_limits<std::uint32_t>::max()) {
      return false;
    }
    count = static_cast<std::uint32_t>(records.size());
    descriptor = kInvalidShaderVisibleIndex;
    if (records.empty()) {
      return true;
    }
    auto allocation = buffer.Allocate(count);
    if (!allocation || !allocation->srv.IsValid()
      || !allocation->TryWriteRange(std::span(records))) {
      return false;
    }
    descriptor = allocation->srv;
    return true;
  };
  auto& bindings = data.bindings;
  std::uint32_t reference_count = 0U;
  if (!write(*directional_record_buffer_, data.directional_records,
        bindings.directional_records_srv, bindings.directional_record_count)
    || !write(*cascade_record_buffer_, data.cascades,
      bindings.cascade_records_srv, bindings.cascade_record_count)
    || !write(*projected_record_buffer_, data.projected_local_records,
      bindings.projected_local_records_srv,
      bindings.projected_local_record_count)
    || !write(*cube_record_buffer_, data.cube_local_records,
      bindings.cube_local_records_srv, bindings.cube_local_record_count)
    || !write(*directional_reference_buffer_,
      data.directional_shadow_references, data.directional_shadow_map_srv,
      reference_count)
    || !write(*local_reference_buffer_, data.local_shadow_references,
      data.local_shadow_map_srv, reference_count)) {
    LOG_F(ERROR, "Shadow record publication failed for view {}", view_id.get());
    return kInvalidShaderVisibleIndex;
  }
  bindings.sampling_flags = kShadowSamplingReversedZPcf;
  return bindings_publisher_->Publish(view_id, bindings);
}

auto ShadowService::PrepareLocalRequests(const FrameShadowInputs& inputs)
  -> void
{
  static const profiling::CpuProfileScopeDesc kProfile { .label
    = "Vortex.Shadows.PrepareLocalRequests",
    .category = profiling::ProfileCategory::kPass };
  const profiling::CpuProfileScope profile(kProfile);
  family_views_.assign(inputs.active_views.begin(), inputs.active_views.end());
  for (auto& view_input : family_views_) {
    const auto revision = view_input.prepared_scene
      ? view_input.prepared_scene->preparation_revision
      : 0;
    const auto epoch
      = inputs.frame_light_set ? inputs.frame_light_set->selection_epoch : 0;
    const auto scene_generation
      = inputs.frame_light_set ? inputs.frame_light_set->scene_generation : 0;
    if (auto old = published_views_.find(view_input.view_id);
      old != published_views_.end()
      && (revision == 0 || old->second.preparation_revision != revision
        || old->second.selection_epoch != epoch
        || old->second.scene_generation != scene_generation)) {
      if (old->second.read_set) {
        old->second.read_set->Close();
      }
      published_views_.erase(old);
    }
    failed_views_.erase(view_input.view_id);
    try {
      view_input.scene_generation = inputs.frame_light_set != nullptr
        ? inputs.frame_light_set->scene_generation
        : 0U;
      if (view_input.prepared_scene != nullptr) {
        const auto& scene = *view_input.prepared_scene;
        auto& prepared = prepared_casters_[view_input.view_id];
        if (prepared.last_seen != current_sequence_
          || scene.preparation_revision == 0
          || prepared.revision != scene.preparation_revision) {
          caster_records_.Build(scene, prepared.dependencies);
          prepared.draw_count = 0;
          for (const auto& draw : scene.GetDrawMetadata()) {
            prepared.draw_count
              += draw.flags.IsSet(PassMaskBit::kShadowCaster) ? 1U : 0U;
          }
          prepared.available
            = prepared.draw_count == 0U || !scene.shadow_caster_sources.empty();
          prepared.revision = scene.preparation_revision;
        }
        prepared.last_seen = current_sequence_;
        view_input.shadow_caster_dependencies = prepared.dependencies;
        view_input.shadow_caster_draw_count = prepared.draw_count;
        view_input.shadow_dependencies_available = prepared.available;
      }
      cascade_shadow_pass_->PrepareLocalRequests(
        { &view_input, 1 }, inputs.frame_light_set);
    } catch (const std::exception& error) {
      LOG_F(ERROR, "Local shadow preparation failed: {}", error.what());
      failed_views_.insert_or_assign(view_input.view_id,
        LightingPreparationFailure {
          .error = LightingPreparationError::kAllocationFailed,
          .view_id = view_input.view_id });
    }
  }
  std::erase_if(family_views_,
    [&](const auto& view) { return failed_views_.contains(view.view_id); });
  cascade_shadow_pass_->ReconcileLocalFamily(family_views_);
}

auto ShadowService::InspectLocalSharing() const -> ShadowSharingDiagnostics
{
  auto result = cascade_shadow_pass_->InspectLocalSharing();
  result.canonical_records = caster_records_.LiveRecordCount();
  result.canonical_record_bytes
    = result.canonical_records * sizeof(ShadowCasterRecord);
  return result;
}

auto ShadowService::RenderShadowDepths(const FrameShadowInputs& inputs) -> void
{
  // Cache the owning label; steady-state scope entry needs no label allocation.
  static const auto kProfile = profiling::CpuProfileScopeDesc {
    .label = "Vortex.Shadows.RecordDepths",
    .category = profiling::ProfileCategory::kPass,
  };
  const auto profile = profiling::CpuProfileScope(kProfile);
  last_render_state_.attached_map_uses = 0;
  last_render_state_.attached_backing_uses = 0;
  last_render_state_.published_view_count = 0U;
  last_render_state_.directional_view_count = 0U;
  last_render_state_.spot_view_count = 0U;
  last_render_state_.point_view_count = 0U;
  last_render_state_.rendered_cascade_count = 0U;
  last_render_state_.rendered_spot_shadow_count = 0U;
  last_render_state_.rendered_point_shadow_count = 0U;
  last_render_state_.rendered_draw_count = 0U;
  last_render_state_.shadow_caster_draw_count = 0U;
  last_render_state_.selection_epoch = inputs.frame_light_set != nullptr
    ? inputs.frame_light_set->selection_epoch
    : 0U;

  const auto directional_lights = inputs.frame_light_set != nullptr
    ? std::span<const FrameDirectionalLightSelection>(
        inputs.frame_light_set->directional_lights)
    : std::span<const FrameDirectionalLightSelection> {};
  cascade_shadow_pass_->RetainDirectionalSources(directional_lights);
  PrepareLocalRequests({ .frame_light_set = inputs.frame_light_set,
    .active_views = inputs.preparation_views.empty()
      ? inputs.active_views
      : inputs.preparation_views });
  for (const auto& active : inputs.active_views) {
    const auto prepared = std::ranges::find(
      family_views_, active.view_id, &PreparedViewShadowInput::view_id);
    if (prepared == family_views_.end()) {
      continue;
    }
    auto view_input = *prepared;
    view_input.view_constants = active.view_constants;
    view_input.lighting_bindings = active.lighting_bindings;
    if (failed_views_.contains(view_input.view_id)) {
      continue;
    }
    try {
      // Reconcile even an empty selection, before any rendering can fail.
      cascade_shadow_pass_->RetainLocalSources(view_input,
        inputs.frame_light_set != nullptr
          ? std::span<const FrameLocalLightSelection>(
              inputs.frame_light_set->local_lights)
          : std::span<const FrameLocalLightSelection> {});
      auto view_data = ShadowFrameData {};
      std::vector<std::shared_ptr<shadows::internal::ShadowMapOwner>>
        local_maps;
      auto directional_surfaces
        = std::vector<std::shared_ptr<graphics::Texture>> {};
      auto spot_shadow_surfaces
        = std::vector<std::shared_ptr<graphics::Texture>> {};
      auto point_shadow_surfaces
        = std::vector<std::shared_ptr<graphics::Texture>> {};
      auto rendered_cascade_count = 0U;
      auto rendered_spot_shadow_count = 0U;
      auto rendered_point_shadow_count = 0U;
      auto rendered_draw_count = 0U;
      auto shadow_caster_draw_count = 0U;

      for (const auto& [index, light] :
        std::views::enumerate(directional_lights)) {
        if (light.cascade_count == 0U
          || !shadows::internal::HasDirectionalShadowInfluence(light)) {
          continue;
        }
        const auto selection_index
          = LightSelectionIndex { static_cast<std::uint32_t>(index) };
        auto view_state = cascade_shadow_pass_->RenderDirectionalView(
          view_input, light, selection_index);
        const auto first_cascade = ShadowCascadeIndex {
          static_cast<std::uint32_t>(view_data.cascades.size())
        };
        for (auto& family : view_state.frame_data.directional_records) {
          family.selection_index = selection_index;
          family.first_cascade = first_cascade;
          view_data.directional_records.push_back(family);
        }
        view_data.cascades.insert(view_data.cascades.end(),
          view_state.frame_data.cascades.begin(),
          view_state.frame_data.cascades.end());
        directional_surfaces.push_back(std::move(view_state.shadow_surface));
        rendered_cascade_count += view_state.rendered_cascade_count;
        rendered_draw_count += view_state.rendered_draw_count;
        shadow_caster_draw_count = (std::max)(shadow_caster_draw_count,
          view_state.shadow_caster_draw_count);
      }

      if (inputs.frame_light_set != nullptr
        && !inputs.frame_light_set->local_lights.empty()) {
        auto spot_state = cascade_shadow_pass_->RenderSpotView(
          view_input, std::span(inputs.frame_light_set->local_lights));
        view_data.projected_local_records = std::move(spot_state.records);
        view_data.local_quality_omissions
          = std::move(spot_state.quality_omissions);
        local_maps.insert(local_maps.end(), spot_state.local_maps.begin(),
          spot_state.local_maps.end());
        spot_shadow_surfaces = std::move(spot_state.shadow_surfaces);
        rendered_spot_shadow_count = spot_state.rendered_shadow_count;
        rendered_draw_count += spot_state.rendered_draw_count;
        shadow_caster_draw_count = (std::max)(shadow_caster_draw_count,
          spot_state.shadow_caster_draw_count);

        auto point_state = cascade_shadow_pass_->RenderPointView(
          view_input, std::span(inputs.frame_light_set->local_lights));
        view_data.cube_local_records = std::move(point_state.records);
        view_data.local_quality_omissions.insert(
          view_data.local_quality_omissions.end(),
          point_state.quality_omissions.begin(),
          point_state.quality_omissions.end());
        local_maps.insert(local_maps.end(), point_state.local_maps.begin(),
          point_state.local_maps.end());
        point_shadow_surfaces = std::move(point_state.shadow_surfaces);
        rendered_point_shadow_count += point_state.rendered_shadow_count;
        rendered_draw_count += point_state.rendered_draw_count;
        shadow_caster_draw_count = (std::max)(shadow_caster_draw_count,
          point_state.shadow_caster_draw_count);
      }

      const auto split_identity
        = [](const std::uint64_t value) -> std::array<std::uint32_t, 2> {
        constexpr auto kHighWordShift = 32U;
        return {
          static_cast<std::uint32_t>(value),
          static_cast<std::uint32_t>(value >> kHighWordShift),
        };
      };
      view_data.bindings.frame_sequence
        = split_identity(current_sequence_.get());
      if (inputs.frame_light_set != nullptr) {
        view_data.bindings.scene_generation
          = split_identity(inputs.frame_light_set->scene_generation);
        view_data.bindings.selection_revision
          = split_identity(inputs.frame_light_set->selection_epoch);
      }
      if (view_input.lighting_bindings != nullptr) {
        view_data.bindings.view_generation
          = view_input.lighting_bindings->view_generation;
        view_data.bindings.view_status_srv
          = view_input.lighting_bindings->build_status_srv;
      }
      if (view_input.resolved_view != nullptr) {
        const auto viewport = view_input.resolved_view->Viewport();
        view_data.bindings.contact_content_origin_px
          = { viewport.top_left_x, viewport.top_left_y };
        view_data.bindings.contact_content_extent_px
          = { viewport.width, viewport.height };
      }
      published_views_.erase(view_input.view_id);
      if (inputs.frame_light_set != nullptr) {
        const auto references = shadows::internal::BuildShadowReferences(
          *inputs.frame_light_set, view_data, view_input.resolved_view.get());
        if (!references) {
          auto failure = references.error();
          failure.view_id = view_input.view_id;
          failed_views_.insert_or_assign(view_input.view_id, failure);
          continue;
        }
      }
      auto contact_surface = std::shared_ptr<graphics::Texture> {};
      const bool needs_contact = inputs.frame_light_set != nullptr
        && shadows::internal::NeedsContactShadows(
          *inputs.frame_light_set, view_input.resolved_view.get());
      if (needs_contact) {
        contact_surface
          = contact_depth_pass_->Record(view_input, view_data.bindings);
        if (!contact_surface) {
          failed_views_.insert_or_assign(view_input.view_id,
            LightingPreparationFailure {
              .error = LightingPreparationError::kMissingShadow,
              .view_id = view_input.view_id,
            });
          continue;
        }
      } else {
        contact_depth_pass_->RemoveView(view_input.view_id);
      }
      const auto before
        = renderer_.GetLightingAllocationBudget()->Snapshot().rejected_requests;
      const auto slot = PublishShadowBindings(view_input.view_id, view_data);
      if (!slot.IsValid()) {
        const auto snapshot
          = renderer_.GetLightingAllocationBudget()->Snapshot();
        const auto budget_failed = snapshot.rejected_requests != before;
        failed_views_.insert_or_assign(view_input.view_id,
          LightingPreparationFailure {
            .error = budget_failed
              ? LightingPreparationError::kBudgetExceeded
              : LightingPreparationError::kAllocationFailed,
            .view_id = view_input.view_id,
            .requested_bytes
            = budget_failed ? snapshot.last_requested : SizeBytes { 0U },
            .available_bytes
            = budget_failed ? snapshot.last_available : SizeBytes { 0U },
          });
        continue;
      }

      last_render_state_.published_view_count += 1U;
      last_render_state_.rendered_cascade_count += rendered_cascade_count;
      last_render_state_.rendered_spot_shadow_count
        += rendered_spot_shadow_count;
      last_render_state_.rendered_point_shadow_count
        += rendered_point_shadow_count;
      last_render_state_.rendered_draw_count += rendered_draw_count;
      last_render_state_.shadow_caster_draw_count += shadow_caster_draw_count;
      if (view_data.bindings.directional_record_count > 0U) {
        last_render_state_.directional_view_count += 1U;
      }
      if (view_data.bindings.projected_local_record_count > 0U) {
        last_render_state_.spot_view_count += 1U;
      }
      if (view_data.bindings.cube_local_record_count > 0U) {
        last_render_state_.point_view_count += 1U;
      }
      const auto revision = view_input.prepared_scene
        ? view_input.prepared_scene->preparation_revision
        : 0;
      auto read_set = std::make_shared<ShadowFrameReadSet>(
        current_sequence_, revision, slot, local_maps);
      if (const auto old = published_views_.find(view_input.view_id);
        old != published_views_.end() && old->second.read_set) {
        old->second.read_set->Close();
      }
      published_views_.insert_or_assign(view_input.view_id,
        PublishedView {
          .slot = slot,
          .data = std::move(view_data),
          .directional_surfaces = std::move(directional_surfaces),
          .spot_surfaces = std::move(spot_shadow_surfaces),
          .point_surfaces = std::move(point_shadow_surfaces),
          .contact_surface = std::move(contact_surface),
          .local_maps = std::move(local_maps),
          .read_set = std::move(read_set),
          .scene_generation = view_input.scene_generation,
          .preparation_revision = revision,
          .selection_epoch = inputs.frame_light_set
            ? inputs.frame_light_set->selection_epoch
            : 0,
        });
    } catch (const graphics::AllocationBudgetExceeded&) {
      const auto snapshot = renderer_.GetLightingAllocationBudget()->Snapshot();
      failed_views_.insert_or_assign(view_input.view_id,
        LightingPreparationFailure {
          .error = LightingPreparationError::kBudgetExceeded,
          .view_id = view_input.view_id,
          .requested_bytes = snapshot.last_requested,
          .available_bytes = snapshot.last_available,
        });
    } catch (const std::exception& error) {
      LOG_F(ERROR, "Shadow preparation/rendering failed for view {}: {}",
        view_input.view_id.get(), error.what());
      failed_views_.insert_or_assign(view_input.view_id,
        LightingPreparationFailure {
          .error = LightingPreparationError::kAllocationFailed,
          .view_id = view_input.view_id,
        });
    }
  }
}

auto ShadowService::InspectPreparationFailure(ViewId view_id) const
  -> const LightingPreparationFailure*
{
  const auto it = failed_views_.find(view_id);
  return it == failed_views_.end() ? nullptr : &it->second;
}

auto ShadowService::InspectShadowData(const ViewId view_id) const
  -> const ShadowFrameData*
{
  const auto it = published_views_.find(view_id);
  return it != published_views_.end() ? &it->second.data : nullptr;
}

auto ShadowService::InspectDirectionalShadowSurfaces(const ViewId view_id) const
  -> std::span<const std::shared_ptr<graphics::Texture>>
{
  const auto it = published_views_.find(view_id);
  return it != published_views_.end()
    ? std::span<const std::shared_ptr<graphics::Texture>>(
        it->second.directional_surfaces)
    : std::span<const std::shared_ptr<graphics::Texture>> {};
}

auto ShadowService::InspectSpotShadowSurfaces(const ViewId view_id) const
  -> std::span<const std::shared_ptr<graphics::Texture>>
{
  const auto it = published_views_.find(view_id);
  return it != published_views_.end()
    ? std::span<const std::shared_ptr<graphics::Texture>>(
        it->second.spot_surfaces)
    : std::span<const std::shared_ptr<graphics::Texture>> {};
}

auto ShadowService::InspectPointShadowSurfaces(const ViewId view_id) const
  -> std::span<const std::shared_ptr<graphics::Texture>>
{
  const auto it = published_views_.find(view_id);
  return it != published_views_.end()
    ? std::span<const std::shared_ptr<graphics::Texture>>(
        it->second.point_surfaces)
    : std::span<const std::shared_ptr<graphics::Texture>> {};
}

auto ShadowService::ResolveShadowFrameSlot(const ViewId view_id) const
  -> ShaderVisibleIndex
{
  const auto it = published_views_.find(view_id);
  return it != published_views_.end()
    ? it->second.slot
    : ShaderVisibleIndex { kInvalidShaderVisibleIndex };
}

auto ShadowService::InspectContactShadowSurface(const ViewId view_id) const
  -> std::shared_ptr<const graphics::Texture>
{
  const auto it = published_views_.find(view_id);
  return it != published_views_.end() ? it->second.contact_surface : nullptr;
}

auto ShadowService::InspectReadSet(ViewId view) const
  -> std::shared_ptr<const ShadowFrameReadSet>
{
  const auto found = published_views_.find(view);
  return found == published_views_.end() ? nullptr : found->second.read_set;
}
auto ShadowService::AttachLocalReads(ViewId view, frame::SequenceNumber frame,
  std::uint64_t preparation_revision, graphics::CommandRecorder& recorder)
  -> void
{
  const auto read_set = InspectReadSet(view);
  if (!read_set) {
    throw std::logic_error("Local shadow publication is unavailable");
  }
  const auto attached = read_set->Attach(frame, preparation_revision, recorder,
    renderer_.GetGraphics()->GetResourceRegistry());
  if (!attached) {
    throw std::logic_error("Local shadow read set is not ready for attachment");
  }
  last_render_state_.attached_map_uses
    += static_cast<uint32_t>(attached->map_uses);
  last_render_state_.attached_backing_uses
    += static_cast<uint32_t>(attached->backing_uses);
}
auto ShadowService::RetainLocalContent(
  ViewId view, scene::NodeHandle light) const -> ShadowContentLease
{
  const auto found = published_views_.find(view);
  if (found == published_views_.end()) {
    return {};
  }
  for (const auto& map : found->second.local_maps) {
    if (map->version->content.light != light) {
      continue;
    }
    auto lease = renderer_.GetGraphics()->GetResourceRegistry().AcquireManaged(
      map->version->slot->backing->registration.Identity());
    if (!lease) {
      return {};
    }
    return ShadowContentLease(map, std::move(*lease));
  }
  return {};
}

} // namespace oxygen::vortex
