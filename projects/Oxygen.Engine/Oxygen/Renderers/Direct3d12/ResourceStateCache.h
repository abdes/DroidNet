//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <unordered_map>
#include <vector>

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Renderers/Direct3d12/ResourceState.h"

namespace oxygen::renderer::d3d12 {

  class D3DResource;

  struct ResourceStateCacheKey
  {
    const D3DResource* resource;
    uint32_t sub_resource;

    ResourceStateCacheKey(const D3DResource* resource, const uint32_t sub_resource_index)
      : resource(resource), sub_resource(sub_resource_index) {
    }

    auto operator<=>(const ResourceStateCacheKey&) const = default;
  };


  class ResourceStateCache final
  {
  public:
    using ResourceStateMap = std::unordered_map<const D3DResource*, ResourceState>;

    ResourceStateCache() = default;
    ~ResourceStateCache();

    OXYGEN_MAKE_NON_COPYABLE(ResourceStateCache);
    OXYGEN_DEFAULT_MOVABLE(ResourceStateCache);

    // State management
    bool EnsureResourceState(const D3DResource& resource,
                             D3D12_RESOURCE_STATES d3d_state,
                             uint32_t sub_resource = ResourceState::InvalidSubResource);
    void FlushPendingBarriers(ID3D12GraphicsCommandList* d3d_command_list);

    // Command buffer lifecycle
    void OnBeginCommandBuffer() const;
    void OnFinishCommandBuffer(ResourceStateMap& out_initial_states,
                               ResourceStateMap& out_final_states);

  private:

    bool PushPendingBarrier(const D3DResource& resource,
                            uint32_t sub_resource,
                            D3D12_RESOURCE_STATES d3d_state_before,
                            D3D12_RESOURCE_STATES d3d_state_after);

    std::vector<D3D12_RESOURCE_BARRIER> pending_barriers_;
    ResourceStateMap cache_;
    ResourceStateMap initial_states_;
  };

  // Helper class for barrier management
  class BarrierFlusher
  {
  public:
    BarrierFlusher(ResourceStateCache& cache, ID3D12GraphicsCommandList* command_list)
      : cache_(cache), command_list_(command_list) {
    }

    ~BarrierFlusher() {
      if (need_flush_) {
        cache_.FlushPendingBarriers(command_list_);
      }
    }

    BarrierFlusher& EnsureResourceState(const D3DResource& resource,
                                        const D3D12_RESOURCE_STATES state,
                                        const uint32_t sub_resource = ResourceState::InvalidSubResource) {
      need_flush_ |= cache_.EnsureResourceState(resource, state, sub_resource);
      return *this;
    }

  private:
    ResourceStateCache& cache_;
    ID3D12GraphicsCommandList* command_list_;
    bool need_flush_ = false;
  };

}  // namespace oxygen::renderer::d3d12
