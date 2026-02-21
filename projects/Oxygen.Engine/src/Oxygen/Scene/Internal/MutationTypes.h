//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <cstdint>
#include <variant>

#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Scene/Types/ScriptSlotIndex.h>

namespace oxygen::scene::internal {

enum class ScriptSlotMutationType : uint8_t {
  kActivated,
  kChanged,
  kDeactivated,
};

struct ScriptSlotMutation {
  ScriptSlotMutationType type { ScriptSlotMutationType::kChanged };
  NodeHandle node_handle {};
  ScriptSlotIndex slot_index {};
};

enum class LightMutationType : uint8_t {
  kChanged,
};

struct LightMutation {
  LightMutationType type { LightMutationType::kChanged };
  NodeHandle node_handle {};
};

enum class CameraMutationType : uint8_t {
  kChanged,
};

struct CameraMutation {
  CameraMutationType type { CameraMutationType::kChanged };
  NodeHandle node_handle {};
};

enum class TransformMutationType : uint8_t {
  kChanged,
};

struct TransformMutation {
  TransformMutationType type { TransformMutationType::kChanged };
  NodeHandle node_handle {};
};

enum class NodeDestroyedMutationType : uint8_t {
  kDestroyed,
};

struct NodeDestroyedMutation {
  NodeDestroyedMutationType type { NodeDestroyedMutationType::kDestroyed };
  NodeHandle node_handle {};
};

using MutationPayload = std::variant<ScriptSlotMutation, LightMutation,
  CameraMutation, TransformMutation, NodeDestroyedMutation>;

struct MutationRecord {
  uint64_t sequence { 0 };
  MutationPayload payload {};
};

template <typename T>
concept SceneMutation
  = std::same_as<T, ScriptSlotMutation> || std::same_as<T, LightMutation>
  || std::same_as<T, CameraMutation> || std::same_as<T, TransformMutation>
  || std::same_as<T, NodeDestroyedMutation>;

} // namespace oxygen::scene::internal
