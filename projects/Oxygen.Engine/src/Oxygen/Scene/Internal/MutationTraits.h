//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Scene/Internal/MutationTypes.h>

namespace oxygen::scene::internal {

template <SceneMutation T> struct MutationTraits;

template <> struct MutationTraits<ScriptSlotMutation> {
  static constexpr bool kCoalescible = false;
};

template <> struct MutationTraits<LightMutation> {
  static constexpr bool kCoalescible = true;
  using KeyType = NodeHandle;

  [[nodiscard]] static auto Key(const LightMutation& mutation) -> KeyType
  {
    return mutation.node_handle;
  }

  static auto Merge(LightMutation& existing, const LightMutation& next) -> void
  {
    existing = next;
  }
};

template <> struct MutationTraits<CameraMutation> {
  static constexpr bool kCoalescible = true;
  using KeyType = NodeHandle;

  [[nodiscard]] static auto Key(const CameraMutation& mutation) -> KeyType
  {
    return mutation.node_handle;
  }

  static auto Merge(CameraMutation& existing, const CameraMutation& next)
    -> void
  {
    existing = next;
  }
};

template <> struct MutationTraits<TransformMutation> {
  // Transform notifications are coalesced by node per sync.
  static constexpr bool kCoalescible = true;
  using KeyType = NodeHandle;

  [[nodiscard]] static auto Key(const TransformMutation& mutation) -> KeyType
  {
    return mutation.node_handle;
  }

  static auto Merge(TransformMutation& existing, const TransformMutation& next)
    -> void
  {
    existing = next;
  }
};

template <> struct MutationTraits<NodeDestroyedMutation> {
  // Destruction notifications are not coalesced; each record is significant.
  static constexpr bool kCoalescible = false;
};

} // namespace oxygen::scene::internal
