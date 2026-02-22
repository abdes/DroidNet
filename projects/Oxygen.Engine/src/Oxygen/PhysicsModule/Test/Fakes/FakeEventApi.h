//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <span>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/Physics/System/IEventApi.h>
#include <Oxygen/PhysicsModule/Test/Fakes/BackendState.h>

namespace oxygen::physics::test::detail {

class FakeEventApi final : public system::IEventApi {
public:
  explicit FakeEventApi(BackendState& state)
    : state_(&state)
  {
  }

  auto GetPendingEventCount(WorldId world_id) const
    -> PhysicsResult<size_t> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    return PhysicsResult<size_t>::Ok(state_->pending_events.size());
  }

  auto DrainEvents(WorldId world_id, std::span<events::PhysicsEvent> out_events)
    -> PhysicsResult<size_t> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    const auto count
      = std::min(out_events.size(), state_->pending_events.size());
    for (size_t i = 0; i < count; ++i) {
      out_events[i] = state_->pending_events[i];
    }
    state_->pending_events.erase(
      state_->pending_events.begin(), state_->pending_events.begin() + count);
    return PhysicsResult<size_t>::Ok(count);
  }

private:
  observer_ptr<BackendState> state_;
};

} // namespace oxygen::physics::test::detail
