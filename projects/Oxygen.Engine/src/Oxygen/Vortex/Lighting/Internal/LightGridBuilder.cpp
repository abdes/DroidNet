//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <unordered_set>
#include <utility>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Profiling/CpuProfileScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Lighting/Internal/LightEvaluationRecords.h>
#include <Oxygen/Vortex/Lighting/Internal/LightGridBuilder.h>
#include <Oxygen/Vortex/Lighting/Types/FrameLightingInputs.h>
#include <Oxygen/Vortex/Lighting/Types/LightGridMetadata.h>
#include <Oxygen/Vortex/Lighting/Types/LightingPreparationFailure.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/LightCullingConfig.h>
#include <Oxygen/Vortex/Types/LightingFrameBindings.h>

namespace oxygen::vortex::lighting::internal {

namespace {

  auto AllocateViewGeneration() -> std::optional<std::uint64_t>
  {
    static std::atomic<std::uint64_t> next { 1U };
    auto candidate = next.load(std::memory_order_relaxed);
    for (;;) {
      if (candidate == std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
      }
      if (next.compare_exchange_weak(
            candidate, candidate + 1U, std::memory_order_relaxed)) {
        return candidate;
      }
    }
  }

  auto ClampViewportDimension(const float value) -> std::uint32_t
  {
    return std::max(1U, static_cast<std::uint32_t>(std::ceil(value)));
  }

  auto BuildGridMetadata(const PreparedViewLightingInput& view_input,
    const bool needs_local_grid) -> std::optional<LightGridMetadata>
  {
    if (view_input.resolved_view == nullptr) {
      return {};
    }

    const auto viewport = view_input.resolved_view->Viewport();
    const auto dims = LightCullingConfig {}.ComputeGridDimensions({
      .width = ClampViewportDimension(viewport.width),
      .height = ClampViewportDimension(viewport.height),
    });
    const bool perspective = !view_input.resolved_view->IsOrthographic();
    auto z_params = LightCullingConfig::ZParams {};
    if (needs_local_grid) {
      const double span
        = static_cast<double>(view_input.resolved_view->FarPlane())
        - view_input.resolved_view->NearPlane();
      if (span < std::numeric_limits<float>::min()
        || span > std::numeric_limits<float>::max()) {
        return std::nullopt;
      }
      if (perspective) {
        const auto resolved = LightCullingConfig::ComputeLightGridZParams(
          view_input.resolved_view->NearPlane(),
          view_input.resolved_view->FarPlane());
        if (!resolved) {
          return std::nullopt;
        }
        z_params = *resolved;
      }
    }

    return LightGridMetadata {
      .grid_size = glm::uvec3 { dims.x, dims.y, dims.z },
      .pixel_size_shift = LightCullingConfig::kLightGridPixelSizeShift,
      .content_origin_px = { viewport.top_left_x, viewport.top_left_y },
      .content_extent_px = { viewport.width, viewport.height },
      .grid_z_params = glm::vec3 { z_params.depth_span_m, z_params.curve_scale,
        z_params.slice_scale },
      .far_depth_m = view_input.resolved_view->FarPlane(),
      .near_depth_m = view_input.resolved_view->NearPlane(),
      .projection_kind
      = perspective ? kLightGridPerspective : kLightGridOrthographic,
    };
  }

  auto SplitGeneration(const std::uint64_t value)
    -> std::array<std::uint32_t, 2>
  {
    return {
      static_cast<std::uint32_t>(value),
      static_cast<std::uint32_t>(value
        >> static_cast<unsigned>(std::numeric_limits<std::uint32_t>::digits)),
    };
  }

} // namespace

auto LightGridBuilder::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  last_build_stats_.frame_sequence = sequence;
  last_build_stats_.frame_slot = slot;
  last_build_stats_.build_count = 0U;
  last_build_stats_.published_view_count = 0U;
  last_build_stats_.directional_light_count = 0U;
  last_build_stats_.local_light_count = 0U;
  last_build_stats_.selection_epoch = 0U;
}

auto LightGridBuilder::Build(const FrameLightingInputs& inputs)
  -> std::expected<BuiltLightGridFrame, LightingPreparationFailure>
{
  // Cache the owning label; steady-state scope entry needs no label allocation.
  static const auto kProfile = profiling::CpuProfileScopeDesc {
    .label = "Vortex.Lighting.BuildGrid",
    .category = profiling::ProfileCategory::kPass,
  };
  const auto profile = profiling::CpuProfileScope(kProfile);
  auto built = BuiltLightGridFrame {};
  if (inputs.frame_light_set == nullptr) {
    return std::unexpected(LightingPreparationFailure {});
  }

  if (inputs.frame_light_set->scene_generation == 0U
    && !inputs.frame_light_set->empty()) {
    return std::unexpected(LightingPreparationFailure {
      .error = LightingPreparationError::kGenerationMismatch,
    });
  }
  if (inputs.active_views.size() > std::numeric_limits<std::uint32_t>::max()) {
    return std::unexpected(LightingPreparationFailure {
      .error = LightingPreparationError::kUnrepresentable,
    });
  }
  auto view_ids = std::unordered_set<ViewId> {};
  view_ids.reserve(inputs.active_views.size());
  for (const auto& view : inputs.active_views) {
    if (view.view_id == kInvalidViewId
      || !view_ids.insert(view.view_id).second) {
      return std::unexpected(
        LightingPreparationFailure { .view_id = view.view_id });
    }
  }

  built.selection_epoch = inputs.frame_light_set->selection_epoch;
  auto evaluation = ResolveLightEvaluationRecords(*inputs.frame_light_set);
  if (!evaluation) {
    return std::unexpected(evaluation.error());
  }
  built.evaluation = std::move(*evaluation);

  built.per_view.reserve(inputs.active_views.size());
  for (const auto& view_input : inputs.active_views) {
    auto failure = LightingPreparationFailure { .view_id = view_input.view_id };
    if (!view_input.resolved_view) {
      return std::unexpected(failure);
    }
    const auto viewport = view_input.resolved_view->Viewport();
    if (!std::isfinite(viewport.width) || !std::isfinite(viewport.height)
      || !std::isfinite(viewport.top_left_x)
      || !std::isfinite(viewport.top_left_y) || viewport.width <= 0.0F
      || viewport.height <= 0.0F
      || static_cast<double>(viewport.width)
        > std::numeric_limits<std::uint32_t>::max()
      || static_cast<double>(viewport.height)
        > std::numeric_limits<std::uint32_t>::max()) {
      return std::unexpected(failure);
    }
    const auto resolved_metadata
      = BuildGridMetadata(view_input, !built.evaluation.local.empty());
    if (!resolved_metadata) {
      failure.error = LightingPreparationError::kUnrepresentable;
      return std::unexpected(failure);
    }
    const auto& metadata = *resolved_metadata;
    const auto cluster_count = static_cast<std::uint64_t>(metadata.grid_size.x)
      * metadata.grid_size.y * metadata.grid_size.z;
    if (cluster_count > std::numeric_limits<std::uint32_t>::max()) {
      failure.error = LightingPreparationError::kUnrepresentable;
      return std::unexpected(failure);
    }
    auto bindings = LightingFrameBindings {};
    bindings.directional_count
      = static_cast<std::uint32_t>(built.evaluation.directional.size());
    bindings.local_count
      = static_cast<std::uint32_t>(built.evaluation.local.size());
    bindings.cluster_count = static_cast<std::uint32_t>(cluster_count);
    bindings.scene_generation
      = SplitGeneration(inputs.frame_light_set->scene_generation);
    bindings.selection_revision = SplitGeneration(built.selection_epoch);
    bindings.frame_sequence
      = SplitGeneration(last_build_stats_.frame_sequence.get());
    const auto generation = AllocateViewGeneration();
    if (!generation) {
      failure.error = LightingPreparationError::kUnrepresentable;
      return std::unexpected(failure);
    }
    bindings.view_generation = SplitGeneration(*generation);
    built.per_view.push_back(BuiltLightGridView {
      .view_id = view_input.view_id,
      .bindings = bindings,
      .metadata = metadata,
      .view_matrix = view_input.resolved_view->ViewMatrix(),
      .projection = view_input.resolved_view->ProjectionMatrix(),
      .inverse_projection = view_input.resolved_view->InverseProjection(),
    });
  }

  last_build_stats_.build_count = 1U;
  last_build_stats_.published_view_count
    = static_cast<std::uint32_t>(built.per_view.size());
  last_build_stats_.directional_light_count
    = inputs.frame_light_set->directional_light_count();
  last_build_stats_.local_light_count
    = inputs.frame_light_set->local_light_count();
  last_build_stats_.selection_epoch = built.selection_epoch;
  return built;
}

} // namespace oxygen::vortex::lighting::internal
