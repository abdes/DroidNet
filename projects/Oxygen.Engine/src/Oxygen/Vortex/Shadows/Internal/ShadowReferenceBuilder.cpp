//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <expected>
#include <limits>
#include <ranges>
#include <utility>
#include <vector>

#include <glm/ext/vector_float3.hpp>

#include <Oxygen/Vortex/Lighting/Types/LightingPreparationFailure.h>
#include <Oxygen/Vortex/Shadows/Internal/ShadowEligibility.h>
#include <Oxygen/Vortex/Shadows/Internal/ShadowReferenceBuilder.h>
#include <Oxygen/Vortex/Shadows/Types/LightShadowReference.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowFrameData.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/LightingIndices.h>

namespace oxygen::vortex::shadows::internal {
auto BuildShadowReferences(const FrameLightSelection& selection,
  ShadowFrameData& data, const ResolvedView* view)
  -> std::expected<void, LightingPreparationFailure>
{
  auto failure = LightingPreparationFailure {};
  constexpr auto kMaxCount = std::numeric_limits<std::uint32_t>::max();
  if (selection.directional_lights.size() > kMaxCount
    || selection.local_lights.size() > kMaxCount
    || data.directional_records.size() > kMaxCount
    || data.projected_local_records.size() > kMaxCount
    || data.cube_local_records.size() > kMaxCount) {
    failure.error = LightingPreparationError::kUnrepresentable;
    return std::unexpected(failure);
  }
  auto directional
    = std::vector<LightShadowReference>(selection.directional_lights.size());
  auto local = std::vector<LightShadowReference>(selection.local_lights.size());
  for (const auto& [index, light] :
    std::views::enumerate(selection.directional_lights)) {
    auto& reference = directional.at(index);
    reference.selection_index
      = LightSelectionIndex { static_cast<std::uint32_t>(index) };
    if ((light.shadow_flags & kDirectionalLightShadowFlagCastsShadows) != 0U) {
      reference.coverage_state = HasDirectionalShadowInfluence(light)
        ? kShadowCoverageComplete
        : kShadowCoverageNoInfluence;
    }
  }
  for (const auto& [index, light] :
    std::views::enumerate(selection.local_lights)) {
    auto& reference = local.at(index);
    reference.selection_index
      = LightSelectionIndex { static_cast<std::uint32_t>(index) };
    if ((light.flags & kLocalLightFlagCastsShadows) != 0U) {
      reference.coverage_state = HasLocalShadowInfluence(light, view)
        ? kShadowCoverageComplete
        : kShadowCoverageNoInfluence;
    }
  }
  for (const auto index : data.local_quality_omissions) {
    if (index.get() >= local.size()
      || local[index.get()].coverage_state != kShadowCoverageComplete) {
      failure.selection_index = index;
      return std::unexpected(failure);
    }
    local[index.get()].coverage_state = kShadowCoverageQualityOmitted;
  }
  const auto associate = [&](auto& references, const LightSelectionIndex source,
                           const std::uint32_t projection,
                           const ShadowRecordIndex record) -> bool {
    failure.selection_index = source;
    if (source.get() >= references.size()) {
      return false;
    }
    auto& reference = references.at(source.get());
    if (reference.coverage_state != kShadowCoverageComplete
      || (reference.record_index != kInvalidShadowRecordIndex)) {
      return false;
    }
    reference.projection_kind = projection;
    reference.record_index = record;
    return true;
  };
  failure.family = LightingSelectionFamily::kDirectional;
  for (const auto& [index, record] :
    std::views::enumerate(data.directional_records)) {
    failure.selection_index = record.selection_index;
    const auto first = record.first_cascade.get();
    if (record.cascade_count == 0U || first > data.cascades.size()
      || record.cascade_count > data.cascades.size() - first) {
      return std::unexpected(failure);
    }
    for (std::uint32_t offset = 0U; offset < record.cascade_count; ++offset) {
      const auto& cascade = data.cascades.at(first + offset);
      if (!cascade.surface_srv.IsValid()
        || cascade.array_layer == kInvalidShadowArrayLayer) {
        return std::unexpected(failure);
      }
    }
    if (!associate(directional, record.selection_index,
          kShadowProjectionCascaded2D,
          ShadowRecordIndex { static_cast<std::uint32_t>(index) })) {
      return std::unexpected(failure);
    }
  }
  failure.family = LightingSelectionFamily::kLocal;
  for (const auto& [index, record] :
    std::views::enumerate(data.projected_local_records)) {
    failure.selection_index = record.selection_index;
    if (!record.surface_srv.IsValid()
      || record.array_layer == kInvalidShadowArrayLayer
      || !associate(local, record.selection_index,
        kShadowProjectionLocalProjected2D,
        ShadowRecordIndex { static_cast<std::uint32_t>(index) })) {
      return std::unexpected(failure);
    }
  }
  for (const auto& [index, record] :
    std::views::enumerate(data.cube_local_records)) {
    failure.selection_index = record.selection_index;
    if (!record.surface_srv.IsValid()
      || record.first_array_layer == kInvalidShadowArrayLayer
      || !associate(local, record.selection_index, kShadowProjectionLocalCube,
        ShadowRecordIndex { static_cast<std::uint32_t>(index) })) {
      return std::unexpected(failure);
    }
  }
  const auto complete = [&](const auto& references) -> bool {
    for (const auto& reference : references) {
      if (reference.coverage_state == kShadowCoverageComplete
        && reference.record_index == kInvalidShadowRecordIndex) {
        failure.error = LightingPreparationError::kMissingShadow;
        failure.selection_index = reference.selection_index;
        return false;
      }
    }
    return true;
  };
  failure.family = LightingSelectionFamily::kDirectional;
  if (!complete(directional)) {
    return std::unexpected(failure);
  }
  failure.family = LightingSelectionFamily::kLocal;
  if (!complete(local)) {
    return std::unexpected(failure);
  }
  data.directional_shadow_references = std::move(directional);
  data.local_shadow_references = std::move(local);
  return {};
}
} // namespace oxygen::vortex::shadows::internal
