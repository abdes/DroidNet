//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <d3d12.h>
#include <dxgidebug.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Component.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>

namespace oxygen::graphics::d3d12 {

//! Enable several debug layer features, including live object reporting, leak
//! tracking, and GPU-based validation.
class DebugLayer final : public Component {
  OXYGEN_COMPONENT(DebugLayer)

public:
  DebugLayer(bool enable_debug, bool enable_validation) noexcept;
  ~DebugLayer() noexcept override;

  OXYGEN_MAKE_NON_COPYABLE(DebugLayer);
  OXYGEN_MAKE_NON_MOVABLE(DebugLayer);

  static auto ConfigureTooling(bool enable_aftermath,
    oxygen::FrameCaptureProvider frame_capture_provider) noexcept -> void;
  static auto BootstrapRenderDoc() noexcept -> void;
  [[nodiscard]] static auto IsPixEnabled() noexcept -> bool;
  static auto ConfigureDeviceInfoQueue(dx::IDevice* device) noexcept -> void;
  static auto ConfigureAftermathForDevice(
    dx::IDevice* device, uint32_t vendor_id) noexcept -> void;
  static auto NotifyDeviceRemoved() noexcept -> void;
  static auto RegisterAftermathResource(ID3D12Resource* resource) noexcept
    -> void;
  static auto UnregisterAftermathResource(ID3D12Resource* resource) noexcept
    -> void;
  static auto SetAftermathMarker(ID3D12GraphicsCommandList* command_list,
    std::string_view marker) noexcept -> void;
  static auto PushAftermathMarker(ID3D12GraphicsCommandList* command_list,
    std::string_view marker) noexcept -> void;
  static auto PopAftermathMarker(
    ID3D12GraphicsCommandList* command_list) noexcept -> void;
  static auto SetAftermathDeviceRemovalContext(
    std::string_view context_info) noexcept -> void;
  static auto PrintDredReport(dx::IDevice* device) noexcept -> void;

private:
  auto InitializeDebugLayer(bool enable_debug, bool enable_validation) noexcept
    -> void;
  auto InitializeDred() noexcept -> void;
  auto InitializeAftermath() noexcept -> void;
  auto PrintLiveObjectsReport() noexcept -> void;

  dx::IDebug* d3d12_debug_ {};
  IDXGIDebug1* dxgi_debug_ {};
  IDXGIInfoQueue* dxgi_info_queue_ {};
  ID3D12DeviceRemovedExtendedDataSettings* dred_settings_ {};
};

} // namespace oxygen::graphics::d3d12
