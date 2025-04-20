//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Direct3D12/Resources/Buffer.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h>
#include <Oxygen/Graphics/Direct3D12/Resources/DeferredObjectRelease.h>

using oxygen::graphics::d3d12::Buffer;

Buffer::~Buffer()
{
    if (allocation_ != nullptr) {
        allocation_->Release();
        allocation_ = nullptr;
    }
    resource_->Release();
    resource_ = nullptr;
}

Buffer::Buffer(const BufferInitInfo& init_info)
    : D3DResource()
{
    try {
        // Create the buffer resource using D3D12MemAlloc
        D3D12MA::Allocation* allocation;
        HRESULT hr = graphics::d3d12::detail::GetAllocator().CreateResource(
            &init_info.alloc_desc,
            &init_info.resource_desc,
            init_info.initial_state,
            nullptr,
            &allocation,
            IID_PPV_ARGS(&resource_));

        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create buffer resource");
        }

        allocation_ = allocation;
        size_ = init_info.size_in_bytes;
    } catch (const std::exception& e) {
        LOG_F(ERROR, "Failed to initialize {}: {}", GetName(), e.what());
        throw;
    }
}

auto Buffer::Map() -> void*
{
    void* mappedData;
    D3D12_RANGE readRange = { 0, 0 };
    HRESULT hr = resource_->Map(0, &readRange, &mappedData);
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to map buffer");
    }
    return mappedData;
}

void Buffer::Unmap()
{
    resource_->Unmap(0, nullptr);
}

void Buffer::SetName(std::string_view name) noexcept
{
    Base::SetName(name);
    NameObject(resource_, name);
}
