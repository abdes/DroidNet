//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <unordered_map>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>

namespace oxygen {
class Graphics;
namespace graphics {
  class CommandQueue;
  class ResourceRegistry;
  namespace detail {
    class ResourceStateTracker;
  }
} // namespace graphics
} // namespace oxygen

namespace oxygen::graphics::headless {

struct CommandContext {
  observer_ptr<Graphics> graphics = nullptr;
  observer_ptr<CommandQueue> queue = nullptr;
  observer_ptr<ResourceRegistry> registry = nullptr;
  detail::ResourceStateTracker* state_tracker = nullptr;
  std::unordered_map<NativeResource, ResourceStates> observed_states;
  uint64_t submission_id = 0;
  std::atomic<bool>* cancel_flag = nullptr;

  auto IsCancelled() const noexcept -> bool
  {
    return cancel_flag && cancel_flag->load(std::memory_order_acquire);
  }
};

} // namespace oxygen::graphics::headless
