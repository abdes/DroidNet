//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>

#include <d3d12.h>

namespace oxygen::renderer::d3d12 {

  //! Represents a D3D12Resource state, which can can be uniform (same across
  //! all sub-resources) or per-sub-resource.
  class ResourceState final
  {
  public:
    using StateType = D3D12_RESOURCE_STATES;
    static constexpr uint32_t InvalidSubResource = std::numeric_limits<uint32_t>::max();

    explicit ResourceState(StateType initial_state = D3D12_RESOURCE_STATE_COMMON);

    [[nodiscard]] StateType GetState(uint32_t sub_resource = InvalidSubResource) const;
    [[nodiscard]] auto IsUniform() const -> bool { return has_uniform_state_; }
    [[nodiscard]] auto SubResourceCount() const -> size_t { return sub_resource_states_.size(); }

    //! Sets the state of the resource or a specific sub-resource.
    /*!
     \param new_state The new state to set.
     \param sub_resource The index of the sub-resource to set the state for. If
            not specified or set to `InvalidSubResource`, the state is applied
            uniformly to the entire resource.
    */
    void SetState(StateType new_state, uint32_t sub_resource = InvalidSubResource);

    //! Checks if the resource (or a specific sub-resource) is in the given state.
    /*!
     \param state The state to check against.
     \param sub_resource The index of the sub-resource to check. When not
            specified, the method checks the uniform state of the resource.
            Ignored if the resource has a uniform state.

     \return `true` if the resource (or the specified sub-resource) is in the
     given state, `false` otherwise.
    */
    [[nodiscard]] bool IsInState(StateType state, uint32_t sub_resource = InvalidSubResource) const;

    [[nodiscard]] bool HasUniformState() const { return has_uniform_state_; }

    //! Checks if all sub-resources are in the same state. If they are, it sets
    //! the resource to have a uniform state.
    void Optimize();

  private:
    void SetUniformState(StateType new_state);

    bool has_uniform_state_{ true };
    StateType uniform_state_{ D3D12_RESOURCE_STATE_COMMON };
    std::vector<StateType> sub_resource_states_{};
  };

}  // namespace oxygen
