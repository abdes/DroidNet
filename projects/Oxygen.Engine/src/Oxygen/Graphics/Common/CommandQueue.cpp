//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <limits>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/ObjectMetadata.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>

using oxygen::graphics::CommandQueue;

auto CommandQueue::GetName() const noexcept -> std::string_view
{
  return GetComponent<ObjectMetadata>().GetName();
}

auto CommandQueue::TryGetTimestampFrequency(uint64_t& /*out_hz*/) const -> bool
{
  return false;
}

auto CommandQueue::BeginProfilingFrame() const -> void { }

auto CommandQueue::TryGetKnownResourceState(
  const NativeResource& resource) const -> std::optional<ResourceStates>
{
  std::lock_guard lock(known_resource_states_mutex_);
  if (const auto it = known_resource_states_.find(resource);
    it != known_resource_states_.end()) {
    return it->second;
  }
  return std::nullopt;
}

auto CommandQueue::AdoptKnownResourceStates(
  const std::span<const KnownResourceState> states) -> void
{
  if (states.empty()) {
    return;
  }

  std::lock_guard lock(known_resource_states_mutex_);
  for (const auto& state : states) {
    if (!state.resource->IsValid() || state.state == ResourceStates::kUnknown) {
      continue;
    }
    known_resource_states_[state.resource] = state.state;
  }
}

auto CommandQueue::ForgetKnownResourceState(const NativeResource& resource)
  -> void
{
  std::lock_guard lock(known_resource_states_mutex_);
  known_resource_states_.erase(resource);
}

void CommandQueue::SetName(const std::string_view name) noexcept
{
  GetComponent<ObjectMetadata>().SetName(name);
}
