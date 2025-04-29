//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DescriptorHeaps.h"
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/Resources/DescriptorHeap.h>
#include <Oxygen/Graphics/Direct3D12/Resources/DescriptorHeaps.h>

using oxygen::graphics::d3d12::detail::DescriptorHeap;
using oxygen::graphics::d3d12::detail::DescriptorHeaps;
using oxygen::graphics::d3d12::detail::GetGraphics;

DescriptorHeaps::DescriptorHeaps() = default;

DescriptorHeaps::~DescriptorHeaps()
{
    delete rtv_heap_;
    delete dsv_heap_;
    delete srv_heap_;
    delete uav_heap_;
}

void DescriptorHeaps::UpdateDependencies(const Composition& composition)
{
    auto& device_manager = composition.GetComponent<oxygen::graphics::d3d12::DeviceManager>();
    auto* device = device_manager.Device();

    rtv_heap_ = new DescriptorHeap {
        DescriptorHeap::InitInfo {
            .type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
            .capacity = 512,
            .is_shader_visible = false,
            .device = device,
            .name = "RTV Descriptor Heap",
        }
    };
    dsv_heap_ = new DescriptorHeap {
        DescriptorHeap::InitInfo {
            .type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
            .capacity = 512,
            .is_shader_visible = false,
            .device = device,
            .name = "DSV Descriptor Heap",
        }
    };
    srv_heap_ = new DescriptorHeap {
        DescriptorHeap::InitInfo {
            .type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            .capacity = 4096,
            .is_shader_visible = true,
            .device = device,
            .name = "SRV Descriptor Heap",
        }
    };
    uav_heap_ = new DescriptorHeap {
        DescriptorHeap::InitInfo {
            .type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            .capacity = 512,
            .is_shader_visible = false,
            .device = device,
            .name = "UAV Descriptor Heap",
        }
    };
}
