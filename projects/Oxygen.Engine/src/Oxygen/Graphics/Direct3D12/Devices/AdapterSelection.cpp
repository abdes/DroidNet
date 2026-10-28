//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <d3dcommon.h>

#include <Oxygen/Graphics/Direct3D12/Devices/AdapterSelection.h>

namespace oxygen::graphics::d3d12 {

namespace {

  auto MatchesPreferredCard(
    const AdapterTraits& adapter, const AdapterSelectionPolicy& policy) -> bool
  {
    if (policy.preferred_card_name.has_value()) {
      return adapter.name.contains(*policy.preferred_card_name);
    }
    return policy.preferred_card_device_id.has_value()
      && std::cmp_equal(adapter.device_id, *policy.preferred_card_device_id);
  }

  //! A selectable adapter and its score; higher is better: feature level
  //! first, then dedicated memory.
  struct Candidate {
    std::size_t index { 0U };
    std::tuple<D3D_FEATURE_LEVEL, std::uint64_t> score;
  };

  auto SortedIndices(std::vector<Candidate>& candidates)
    -> std::vector<std::size_t>
  {
    std::ranges::stable_sort(
      candidates, std::ranges::greater {}, &Candidate::score);
    auto indices = std::vector<std::size_t> {};
    indices.reserve(candidates.size());
    for (const auto& candidate : candidates) {
      indices.push_back(candidate.index);
    }
    return indices;
  }

} // namespace

auto to_string(const AdapterChoiceReason reason) -> std::string_view
{
  switch (reason) {
  case AdapterChoiceReason::kPreferredCard:
    return "matches the preferred card";
  case AdapterChoiceReason::kScore:
    return "best feature level and dedicated memory";
  case AdapterChoiceReason::kNoPreferredMatch:
    return "no usable adapter matches the preferred card; best feature level "
           "and dedicated memory";
  }
  return "__NotSupported__";
}

auto RankAdapters(const std::span<const AdapterTraits> adapters,
  const AdapterSelectionPolicy& policy) -> AdapterRanking
{
  const auto has_hint = policy.preferred_card_name.has_value()
    || policy.preferred_card_device_id.has_value();
  if (policy.preferred_card_name.has_value()
    && policy.preferred_card_device_id.has_value()) {
    throw std::invalid_argument(
      "Adapter selection cannot prefer both a card name and a card device id");
  }

  auto preferred = std::vector<Candidate> {};
  auto scored = std::vector<Candidate> {};
  for (std::size_t index = 0U; index < adapters.size(); ++index) {
    const auto& adapter = adapters[index];
    if (!adapter.max_feature_level.has_value()) {
      continue;
    }
    const auto candidate = Candidate {
      .index = index,
      .score = { *adapter.max_feature_level, adapter.dedicated_memory },
    };
    if (has_hint && MatchesPreferredCard(adapter, policy)) {
      preferred.push_back(candidate);
    } else if (!policy.require_display || adapter.has_display) {
      scored.push_back(candidate);
    }
  }

  auto ranking = AdapterRanking {
    .order = SortedIndices(preferred),
    .reason = AdapterChoiceReason::kScore,
  };
  if (has_hint) {
    ranking.reason = ranking.order.empty()
      ? AdapterChoiceReason::kNoPreferredMatch
      : AdapterChoiceReason::kPreferredCard;
  }
  ranking.order.append_range(SortedIndices(scored));
  return ranking;
}

} // namespace oxygen::graphics::d3d12
