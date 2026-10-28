//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <d3dcommon.h>

#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

namespace oxygen::graphics::d3d12 {

//! What adapter ranking reads of one discovered hardware adapter.
struct AdapterTraits {
  std::string_view name;
  std::uint32_t device_id { 0U };
  std::uint64_t dedicated_memory { 0U };
  bool has_display { false };
  //! Highest supported feature level; none when the adapter does not support
  //! the required minimum.
  std::optional<D3D_FEATURE_LEVEL> max_feature_level;
};

//! The configured adapter selection policy.
/*!
  An adapter below the minimum feature level is never selected. A usable
  adapter matching a hint comes first, whether or not it drives a display.
  When `require_display` is set, the other adapters must drive a display. The
  remaining adapters follow by score: the highest feature level, then the most
  dedicated memory, then enumeration order.
*/
struct AdapterSelectionPolicy {
  bool require_display { false };
  //! Mutually exclusive hints: a part of the adapter name, or its PCI device
  //! id.
  std::optional<std::string> preferred_card_name;
  std::optional<DeviceId> preferred_card_device_id;
};

//! Why the first adapter of a ranking is first.
enum class AdapterChoiceReason : std::uint8_t {
  //! It matches the hint.
  kPreferredCard,
  //! No hint was given; it has the best score.
  kScore,
  //! No usable adapter matches the hint; it has the best score.
  kNoPreferredMatch,
};

OXGN_D3D12_NDAPI auto to_string(AdapterChoiceReason reason) -> std::string_view;

struct AdapterRanking {
  //! Indices of the selectable adapters, most suitable first.
  std::vector<std::size_t> order;
  AdapterChoiceReason reason { AdapterChoiceReason::kScore };
};

//! Order the selectable adapters by the policy.
/*!
  @throw std::invalid_argument When the policy sets both hints.
*/
OXGN_D3D12_NDAPI auto RankAdapters(std::span<const AdapterTraits> adapters,
  const AdapterSelectionPolicy& policy) -> AdapterRanking;

} // namespace oxygen::graphics::d3d12
