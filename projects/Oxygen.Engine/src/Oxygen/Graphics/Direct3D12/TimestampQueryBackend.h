//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>

#include <wrl/client.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/TimestampQueryProvider.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

namespace oxygen::graphics::d3d12 {

class Graphics;

class TimestampQueryBackend final : public graphics::TimestampQueryProvider {
public:
  OXGN_D3D12_API explicit TimestampQueryBackend(
    const Graphics& graphics, uint32_t initial_capacity_queries = 8192U);
  OXGN_D3D12_API ~TimestampQueryBackend() override;

  OXYGEN_MAKE_NON_COPYABLE(TimestampQueryBackend)
  OXYGEN_MAKE_NON_MOVABLE(TimestampQueryBackend)

  OXGN_D3D12_API auto EnsureCapacity(uint32_t required_query_count)
    -> bool override;

  [[nodiscard]] auto GetCapacity() const noexcept -> uint32_t override
  {
    return capacity_queries_;
  }

  OXGN_D3D12_API auto WriteTimestamp(
    graphics::CommandRecorder& recorder, uint32_t query_slot) -> bool override;

  OXGN_D3D12_API auto RecordResolve(graphics::CommandRecorder& recorder,
    uint32_t used_query_slots) -> bool override;

  [[nodiscard]] OXGN_D3D12_API auto GetResolvedTicks() const
    -> std::span<const uint64_t> override;

private:
  auto RecreateResources(uint32_t capacity_queries) -> bool;
  auto ReleaseResources() noexcept -> void;

  const Graphics& graphics_;
  Microsoft::WRL::ComPtr<ID3D12QueryHeap> query_heap_ {};
  Microsoft::WRL::ComPtr<ID3D12Resource> readback_resource_ {};
  uint64_t* mapped_ticks_ { nullptr };
  uint32_t capacity_queries_ { 0 };
};

} // namespace oxygen::graphics::d3d12
