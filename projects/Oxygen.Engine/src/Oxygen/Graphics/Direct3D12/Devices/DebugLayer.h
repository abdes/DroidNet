//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>
#include <dxgidebug.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Component.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>

namespace oxygen::graphics::d3d12 {

//! Enable several debug layer features, including live object reporting, leak
//! tracking, and GPU-based validation.
class DebugLayer final : public Component {
  OXYGEN_COMPONENT(DebugLayer)

public:
  explicit DebugLayer(bool enable_validation) noexcept;
  ~DebugLayer() noexcept override;

  OXYGEN_MAKE_NON_COPYABLE(DebugLayer);
  OXYGEN_MAKE_NON_MOVABLE(DebugLayer);

  static auto PrintDredReport(dx::IDevice* device) noexcept -> void;

private:
  auto InitializeDebugLayer(bool enable_validation) noexcept -> void;
  auto InitializeDred() noexcept -> void;
  auto PrintLiveObjectsReport() noexcept -> void;

  ID3D12Debug6* d3d12_debug_ {};
  IDXGIDebug1* dxgi_debug_ {};
  IDXGIInfoQueue* dxgi_info_queue_ {};
  ID3D12DeviceRemovedExtendedDataSettings* dred_settings_ {};
};

} // namespace oxygen::graphics::d3d12
