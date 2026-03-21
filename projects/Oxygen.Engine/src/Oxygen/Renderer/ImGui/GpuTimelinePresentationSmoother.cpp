//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <limits>
#include <string_view>
#include <unordered_set>

#include <Oxygen/Renderer/ImGui/GpuTimelinePresentationSmoother.h>

namespace oxygen::engine::imgui {

namespace {

constexpr uint32_t kInvalidScopeId = 0xFFFFFFFFU;

auto BlendValue(const float previous, const float current, const float alpha)
  -> float
{
  return previous + ((current - previous) * alpha);
}

auto HashCombine(const uint64_t seed, const uint64_t value) -> uint64_t
{
  return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

auto MakeBaseScopeKey(const uint64_t parent_key,
  const internal::GpuTimelineScope& scope) -> uint64_t
{
  auto key = HashCombine(parent_key, scope.name_hash);
  key = HashCombine(key, scope.depth);
  key = HashCombine(key, scope.stream_id);
  return key;
}

auto MakeStableScopeKey(
  const uint64_t base_key, const uint32_t sibling_ordinal) -> uint64_t
{
  return HashCombine(base_key, sibling_ordinal);
}

auto IsRenderPassPhaseName(const std::string_view name) -> bool
{
  return name == "PrepareResources" || name == "Execute";
}

auto MakePresentationRow(
  const GpuTimelinePresentationScope& scope, const uint16_t depth)
  -> GpuTimelinePresentationRow
{
  return GpuTimelinePresentationRow {
    .display_name = scope.display_name,
    .depth = depth,
    .grouped_scope_count = 1U,
    .valid = scope.valid,
    .is_grouped_pass = false,
    .raw_start_ms = scope.raw_start_ms,
    .raw_duration_ms = scope.raw_duration_ms,
    .raw_end_ms = scope.raw_end_ms,
    .display_start_ms = scope.display_start_ms,
    .display_duration_ms = scope.display_duration_ms,
    .display_end_ms = scope.display_end_ms,
  };
}

} // namespace

auto GpuTimelinePresentationSmoother::Apply(
  const internal::GpuTimelineFrame& frame) -> GpuTimelinePresentationFrame
{
  GpuTimelinePresentationFrame presentation {};
  presentation.frame_sequence = frame.frame_sequence;
  presentation.scopes.reserve(frame.scopes.size());
  presentation.rows.reserve(frame.scopes.size());

  std::vector<uint64_t> stable_scope_keys(frame.scopes.size(), 0U);
  std::unordered_map<uint64_t, uint32_t> sibling_counts {};
  sibling_counts.reserve(frame.scopes.size());
  std::unordered_set<uint64_t> seen_scope_keys {};
  seen_scope_keys.reserve(frame.scopes.size());

  float raw_frame_span_ms = 0.0F;
  for (std::size_t i = 0; i < frame.scopes.size(); ++i) {
    const auto& scope = frame.scopes[i];
    const auto parent_key
      = scope.parent_scope_id < stable_scope_keys.size()
      ? stable_scope_keys[scope.parent_scope_id]
      : 0U;
    const auto base_key = MakeBaseScopeKey(parent_key, scope);
    const auto sibling_ordinal = sibling_counts[base_key]++;
    const auto stable_key = MakeStableScopeKey(base_key, sibling_ordinal);
    stable_scope_keys[i] = stable_key;

    GpuTimelinePresentationScope entry {};
    entry.scope_id = scope.scope_id;
    entry.parent_scope_id = scope.parent_scope_id;
    entry.stable_key = stable_key;
    entry.name_hash = scope.name_hash;
    entry.display_name = scope.display_name;
    entry.depth = scope.depth;
    entry.stream_id = scope.stream_id;
    entry.valid = scope.valid;
    entry.raw_start_ms = scope.start_ms;
    entry.raw_duration_ms = scope.duration_ms;
    entry.raw_end_ms = scope.end_ms;

    if (scope.valid) {
      const auto [state_it, inserted] = scope_state_.try_emplace(stable_key,
        SmoothedScopeState {
          .start_ms = scope.start_ms,
          .duration_ms = scope.duration_ms,
        });
      if (!inserted) {
        state_it->second.start_ms = BlendValue(
          state_it->second.start_ms, scope.start_ms, kDefaultBlendFactor);
        state_it->second.duration_ms = BlendValue(state_it->second.duration_ms,
          scope.duration_ms, kDefaultBlendFactor);
      }

      entry.display_start_ms = std::max(0.0F, state_it->second.start_ms);
      entry.display_duration_ms = std::max(0.0F, state_it->second.duration_ms);
      entry.display_end_ms = entry.display_start_ms + entry.display_duration_ms;
      raw_frame_span_ms = std::max(raw_frame_span_ms, scope.end_ms);
      seen_scope_keys.insert(stable_key);
    }

    presentation.scopes.push_back(entry);
  }

  presentation.raw_frame_span_ms = raw_frame_span_ms;
  if (has_smoothed_frame_span_) {
    smoothed_frame_span_ms_ = BlendValue(
      smoothed_frame_span_ms_, raw_frame_span_ms, kDefaultBlendFactor);
  } else {
    smoothed_frame_span_ms_ = raw_frame_span_ms;
    has_smoothed_frame_span_ = true;
  }
  presentation.display_frame_span_ms = std::max(
    presentation.raw_frame_span_ms, smoothed_frame_span_ms_);

  for (auto it = scope_state_.begin(); it != scope_state_.end();) {
    if (!seen_scope_keys.contains(it->first)) {
      it = scope_state_.erase(it);
    } else {
      ++it;
    }
  }

  if (presentation.scopes.empty()) {
    smoothed_frame_span_ms_ = 0.0F;
    has_smoothed_frame_span_ = false;
    return presentation;
  }

  std::unordered_map<uint32_t, std::vector<std::size_t>> child_indices_by_parent {};
  child_indices_by_parent.reserve(presentation.scopes.size());
  for (std::size_t i = 0; i < presentation.scopes.size(); ++i) {
    child_indices_by_parent[presentation.scopes[i].parent_scope_id].push_back(i);
  }

  const auto is_groupable_pass_scope =
    [&](const std::size_t scope_index) -> bool {
    const auto& scope = presentation.scopes[scope_index];
    const auto children_it = child_indices_by_parent.find(scope.scope_id);
    if (children_it == child_indices_by_parent.end()
      || children_it->second.size() != 1U) {
      return false;
    }

    const auto& phase_scope
      = presentation.scopes[children_it->second.front()];
    return IsRenderPassPhaseName(phase_scope.display_name);
  };

  const auto make_grouped_pass_row =
    [&](const std::vector<std::size_t>& grouped_scope_indices,
        const uint16_t depth) -> GpuTimelinePresentationRow {
    const auto& first_scope = presentation.scopes[grouped_scope_indices.front()];
    bool all_valid = true;
    float raw_start_ms = std::numeric_limits<float>::max();
    float raw_end_ms = 0.0F;
    float display_start_ms = std::numeric_limits<float>::max();
    float display_end_ms = 0.0F;

    for (const auto scope_index : grouped_scope_indices) {
      const auto& scope = presentation.scopes[scope_index];
      all_valid = all_valid && scope.valid;
      if (!scope.valid) {
        continue;
      }

      raw_start_ms = std::min(raw_start_ms, scope.raw_start_ms);
      raw_end_ms = std::max(raw_end_ms, scope.raw_end_ms);
      display_start_ms = std::min(display_start_ms, scope.display_start_ms);
      display_end_ms = std::max(display_end_ms, scope.display_end_ms);
    }

    return GpuTimelinePresentationRow {
      .display_name = first_scope.display_name,
      .depth = depth,
      .grouped_scope_count = static_cast<uint32_t>(grouped_scope_indices.size()),
      .valid = all_valid,
      .is_grouped_pass = true,
      .raw_start_ms = all_valid ? raw_start_ms : 0.0F,
      .raw_duration_ms = all_valid ? (raw_end_ms - raw_start_ms) : 0.0F,
      .raw_end_ms = all_valid ? raw_end_ms : 0.0F,
      .display_start_ms = all_valid ? display_start_ms : 0.0F,
      .display_duration_ms
        = all_valid ? (display_end_ms - display_start_ms) : 0.0F,
      .display_end_ms = all_valid ? display_end_ms : 0.0F,
    };
  };

  const auto try_collect_grouped_pass_run =
    [&](const std::vector<std::size_t>& sibling_scope_indices,
        const std::size_t start_index) -> std::vector<std::size_t> {
    std::vector<std::size_t> grouped_scope_indices {};
    const auto first_scope_index = sibling_scope_indices[start_index];
    if (!is_groupable_pass_scope(first_scope_index)) {
      return grouped_scope_indices;
    }

    const auto& first_scope = presentation.scopes[first_scope_index];
    std::unordered_set<uint64_t> phase_name_hashes {};

    for (std::size_t i = start_index; i < sibling_scope_indices.size(); ++i) {
      const auto scope_index = sibling_scope_indices[i];
      const auto& scope = presentation.scopes[scope_index];
      if (!is_groupable_pass_scope(scope_index)
        || scope.name_hash != first_scope.name_hash
        || scope.stream_id != first_scope.stream_id) {
        break;
      }

      const auto& phase_scope
        = presentation.scopes[child_indices_by_parent[scope.scope_id].front()];
      if (!phase_name_hashes.insert(phase_scope.name_hash).second) {
        break;
      }

      grouped_scope_indices.push_back(scope_index);
    }

    if (grouped_scope_indices.size() < 2U) {
      grouped_scope_indices.clear();
    }

    return grouped_scope_indices;
  };

  const auto emit_scope_subtree =
    [&](const auto& self, const std::size_t scope_index, const uint16_t depth)
    -> void {
    const auto& scope = presentation.scopes[scope_index];
    presentation.rows.push_back(MakePresentationRow(scope, depth));

    const auto children_it = child_indices_by_parent.find(scope.scope_id);
    if (children_it == child_indices_by_parent.end()) {
      return;
    }

    const auto& sibling_scope_indices = children_it->second;
    for (std::size_t i = 0; i < sibling_scope_indices.size();) {
      const auto grouped_scope_indices
        = try_collect_grouped_pass_run(sibling_scope_indices, i);
      if (!grouped_scope_indices.empty()) {
        presentation.rows.push_back(
          make_grouped_pass_row(grouped_scope_indices, depth + 1U));
        for (const auto grouped_scope_index : grouped_scope_indices) {
          const auto& grouped_scope = presentation.scopes[grouped_scope_index];
          const auto& phase_scope_indices
            = child_indices_by_parent[grouped_scope.scope_id];
          for (const auto phase_scope_index : phase_scope_indices) {
            self(self, phase_scope_index, depth + 2U);
          }
        }
        i += grouped_scope_indices.size();
        continue;
      }

      self(self, sibling_scope_indices[i], depth + 1U);
      ++i;
    }
  };

  const auto root_scopes_it = child_indices_by_parent.find(kInvalidScopeId);
  if (root_scopes_it != child_indices_by_parent.end()) {
    for (const auto root_scope_index : root_scopes_it->second) {
      emit_scope_subtree(emit_scope_subtree, root_scope_index, 0U);
    }
  }

  return presentation;
}

} // namespace oxygen::engine::imgui
