//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Renderers/Direct3d12/ResourceState.h"

#include <algorithm>

#include "Oxygen/Base/Logging.h"

namespace oxygen::renderer::d3d12 {

  ResourceState::ResourceState(const StateType initial_state)
    : uniform_state_(initial_state)
  {
  }

  void ResourceState::SetUniformState(const StateType new_state) {
    has_uniform_state_ = true;
    uniform_state_ = new_state;
    sub_resource_states_.clear();
  }

  void ResourceState::SetState(const StateType new_state, const uint32_t sub_resource) {
    if (sub_resource == InvalidSubResource) {
      return SetUniformState(new_state);
    }

    if (has_uniform_state_ && uniform_state_ == new_state) {
      return;
    }

    if (sub_resource_states_.size() <= sub_resource) {
      sub_resource_states_.resize(sub_resource + 1, uniform_state_);
    }

    has_uniform_state_ = false;
    sub_resource_states_[sub_resource] = new_state;
  }

  ResourceState::StateType ResourceState::GetState(const uint32_t sub_resource) const {

    if (has_uniform_state_ || sub_resource == InvalidSubResource) {
      return uniform_state_;
    }

    return sub_resource < sub_resource_states_.size()
      ? sub_resource_states_[sub_resource]
      : uniform_state_;
  }

  bool ResourceState::IsInState(const StateType state, const uint32_t sub_resource) const {
    if (sub_resource == InvalidSubResource) {
      return has_uniform_state_ && uniform_state_ == state;
    }

    return GetState(sub_resource) == state;
  }

  void ResourceState::Optimize() {
    if (has_uniform_state_ || sub_resource_states_.empty()) {
      return;
    }

    const StateType first_state = sub_resource_states_[0];
    const bool all_same = std::ranges::all_of(
      sub_resource_states_,
      [first_state](const StateType state)
      {
        return state == first_state;
      }
    );

    if (all_same) {
      SetUniformState(first_state);
    }
  }

}  // namespace oxygen::renderer::d3d12
