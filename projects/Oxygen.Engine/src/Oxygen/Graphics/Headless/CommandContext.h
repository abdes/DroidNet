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

namespace oxygen::graphics {
class Graphics;
class CommandQueue;
class ResourceRegistry;
namespace detail {
  class ResourceStateTracker;
}
}

namespace oxygen::graphics::headless {

struct CommandContext {
  observer_ptr<oxygen::graphics::Graphics> graphics = nullptr;
  observer_ptr<oxygen::graphics::CommandQueue> queue = nullptr;
  observer_ptr<oxygen::graphics::ResourceRegistry> registry = nullptr;
  oxygen::graphics::detail::ResourceStateTracker* state_tracker = nullptr;
  std::unordered_map<::oxygen::graphics::NativeObject,
    ::oxygen::graphics::ResourceStates>
    observed_states;
  uint64_t submission_id = 0;
  std::atomic<bool>* cancel_flag = nullptr;

  bool IsCancelled() const noexcept
  {
    return cancel_flag && cancel_flag->load(std::memory_order_acquire);
  }
};

} // namespace oxygen::graphics::headless
