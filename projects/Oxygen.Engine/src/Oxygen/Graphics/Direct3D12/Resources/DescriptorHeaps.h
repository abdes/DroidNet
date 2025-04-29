//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/ComponentMacros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DeviceManager.h>
#include <Oxygen/Graphics/Direct3D12/Resources/DescriptorHeap.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

namespace oxygen::graphics::d3d12::detail {

class DescriptorHeaps final : public Component {
    OXYGEN_COMPONENT(DescriptorHeaps)
    OXYGEN_COMPONENT_REQUIRES(oxygen::graphics::d3d12::DeviceManager)

public:
    OXYGEN_D3D12_API DescriptorHeaps();
    OXYGEN_D3D12_API ~DescriptorHeaps() override;

    OXYGEN_MAKE_NON_COPYABLE(DescriptorHeaps)
    OXYGEN_MAKE_NON_MOVABLE(DescriptorHeaps)

    [[nodiscard]] auto RtvHeap() const -> DescriptorHeap& { return *rtv_heap_; }
    [[nodiscard]] auto UavHeap() const -> DescriptorHeap& { return *uav_heap_; }
    [[nodiscard]] auto SrvHeap() const -> DescriptorHeap& { return *srv_heap_; }
    [[nodiscard]] auto DsvHeap() const -> DescriptorHeap& { return *dsv_heap_; }

protected:
    void UpdateDependencies(const Composition& composition) override;

private:
    mutable DescriptorHeap* rtv_heap_ { nullptr };
    mutable DescriptorHeap* dsv_heap_ { nullptr };
    mutable DescriptorHeap* srv_heap_ { nullptr };
    mutable DescriptorHeap* uav_heap_ { nullptr };
};

} // namespace oxygen::graphics::d3d12::detail
