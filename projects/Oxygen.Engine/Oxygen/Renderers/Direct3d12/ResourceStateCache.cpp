//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Renderers/Direct3d12/ResourceStateCache.h"

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Renderers/Common/Types.h"
#include "Oxygen/Renderers/Direct3d12/D3DResource.h"

namespace oxygen::renderer::d3d12 {


  ResourceStateCache::~ResourceStateCache()
  {
    DCHECK_F(pending_barriers_.empty(), "All barriers should be flushed");
    DCHECK_F(initial_states_.empty(), "Initial states cache should be empty");
    DCHECK_F(cache_.empty(), "States cache should be empty");
  }

  void ResourceStateCache::OnBeginCommandBuffer() const
  {
    DCHECK_F(pending_barriers_.empty(), "All barriers should be empty");
    DCHECK_F(initial_states_.empty(), "Initial states cache should be empty");
    DCHECK_F(cache_.empty(), "States cache should be empty");
  }

  void ResourceStateCache::OnFinishCommandBuffer(
    ResourceStateMap& out_initial_states,
    ResourceStateMap& out_final_states)
  {
    DCHECK_F(pending_barriers_.empty(), "All barriers should be empty");

    out_initial_states = std::move(initial_states_);
    out_final_states = std::move(cache_);
  }

  bool ResourceStateCache::EnsureResourceState(
    const D3DResource& resource,
    const D3D12_RESOURCE_STATES d3d_state,
    const uint32_t sub_resource)
  {

    if (resource.GetMode() == ResourceAccessMode::kImmutable)
    {
      if (d3d_state != D3D12_RESOURCE_STATE_COMMON)
      {
        constexpr D3D12_RESOURCE_STATES allowed_states =
          D3D12_RESOURCE_STATE_GENERIC_READ |
          D3D12_RESOURCE_STATE_COPY_DEST;
        DCHECK_EQ_F((d3d_state & ~allowed_states), 0, "Illegal immutable resource state");
      }

      // Immutable resources are in COMMON state and are promoted to appropriate
      // read state automatically. The only exception is the first resource upload.
      return false;
    }

    if (resource.GetMode() == ResourceAccessMode::kUpload
        || resource.GetMode() == ResourceAccessMode::kReadBack)
    {
      // Upload/ReadBack resources are in GENERIC_READ/COPY_DEST all the time
      return false;
    }

    bool pushed_any_barrier = false;

    const auto it = cache_.find(&resource);
    if (cache_.contains(&resource))
    {
      ResourceState& state = it->second;

      if (sub_resource == UINT32_MAX)
      {
        if (state.IsUniform())
        {
          const D3D12_RESOURCE_STATES prev_state = state.GetState();
          pushed_any_barrier |= PushPendingBarrier(resource, UINT32_MAX, prev_state, d3d_state);
        }
        else
        {
          for (auto i = 0U; i < state.SubResourceCount(); ++i)
          {
            pushed_any_barrier |= PushPendingBarrier(resource, i, state.GetState(i), d3d_state);
          }
        }
      }
      else
      {
        const D3D12_RESOURCE_STATES prev_state = state.GetState(sub_resource);
        pushed_any_barrier |= PushPendingBarrier(resource, sub_resource, prev_state, d3d_state);
      }

      // update state in the cache
      state.SetState(d3d_state, sub_resource);
    }
    else
    {
      // TODO: if setting a sub-resource state, it will default all other
      // sub-resource states to D3D12_RESOURCE_STATE_COMMON
      // Ideally, other sub-resource states should be undefined

      ResourceState newState;
      newState.SetState(d3d_state, sub_resource);

      cache_.emplace(&resource, newState);
      initial_states_.emplace(&resource, newState);
    }

    return pushed_any_barrier;
  }

  bool ResourceStateCache::PushPendingBarrier(
    const D3DResource& resource,
    const uint32_t sub_resource,
    const D3D12_RESOURCE_STATES d3d_state_before,
    const D3D12_RESOURCE_STATES d3d_state_after)
  {
    if (d3d_state_before == d3d_state_after)
    {
      return false;
    }

    // TODO: check for duplicate barriers

    if (!pending_barriers_.empty())
    {
      // update existing barrier
      if (D3D12_RESOURCE_BARRIER& last_barrier = pending_barriers_.back();
          last_barrier.Transition.pResource == resource.GetResource() &&
          last_barrier.Transition.Subresource == sub_resource &&
          last_barrier.Transition.StateAfter == d3d_state_before)
      {
        last_barrier.Transition.StateAfter = d3d_state_after;

        if (last_barrier.Transition.StateBefore == last_barrier.Transition.StateAfter)
        {
          pending_barriers_.pop_back();
          return false;
        }

        return true;
      }
    }

    const D3D12_RESOURCE_BARRIER barrier_desc = {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
        .Transition = {
            .pResource = resource.GetResource(),
            .Subresource = sub_resource,
            .StateBefore = d3d_state_before,
            .StateAfter = d3d_state_after,
        },
    };

    pending_barriers_.push_back(barrier_desc);

    return true;
  }

  void ResourceStateCache::FlushPendingBarriers(ID3D12GraphicsCommandList* d3d_command_list)
  {
    DCHECK_NOTNULL_F(d3d_command_list, "invalid command list");

    if (!pending_barriers_.empty())
    {
      d3d_command_list->ResourceBarrier(
        static_cast<uint32_t>(pending_barriers_.size()),
        pending_barriers_.data());

      pending_barriers_.clear();
    }
  }

} // namespace oxygen::renderer::d3d12
