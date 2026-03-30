//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

namespace oxygen::graphics::d3d12 {

// Lightweight singleton facade around NVIDIA Nsight Aftermath integration.
// Methods are no-op when Aftermath support is not compiled in.
class AftermathTracker final {
public:
  static auto Instance() -> AftermathTracker&;
  [[nodiscard]] static auto IsSdkAvailable() noexcept -> bool;

  auto EnableCrashDumps() noexcept -> void;
  auto DisableCrashDumps() noexcept -> void;

  auto InitializeDevice(ID3D12Device* device, uint32_t vendor_id) noexcept
    -> void;
  auto WaitForCrashDumpCompletion() noexcept -> void;

  auto RegisterResource(ID3D12Resource* resource) noexcept -> void;
  auto UnregisterResource(ID3D12Resource* resource) noexcept -> void;

  auto SetMarker(ID3D12GraphicsCommandList* command_list,
    std::string_view marker_text) noexcept -> void;

  auto PushMarker(ID3D12GraphicsCommandList* command_list,
    std::string_view marker_text) noexcept -> void;
  auto PopMarker(ID3D12GraphicsCommandList* command_list) noexcept -> void;

  auto NotifyDeviceRemovalContext(std::string_view context_info) noexcept
    -> void;

private:
  AftermathTracker() = default;
};

} // namespace oxygen::graphics::d3d12
