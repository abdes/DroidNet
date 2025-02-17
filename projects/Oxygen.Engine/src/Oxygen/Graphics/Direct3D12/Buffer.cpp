//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Direct3D12/Buffer.h>

#include "DeferredObjectRelease.h"

using oxygen::graphics::d3d12::Buffer;

Buffer::~Buffer()
{
    if (should_release_) {
        LOG_F(ERROR, "You should call Release() before the Disposable object is destroyed!");
        const auto stack_trace = loguru::stacktrace();
        if (!stack_trace.empty()) {
            DRAW_LOG_F(ERROR, "{}", stack_trace.c_str());
        }
        ABORT_F("Cannot continue!");
    }
}

void Buffer::Initialize(const BufferInitInfo& init_info)
{
    if (should_release_) {
        const auto msg = fmt::format("{} OnInitialize() called twice without calling Release()", this->self().ObjectName());
        LOG_F(ERROR, "{}", msg);
        throw std::runtime_error(msg);
    }
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

        should_release_ = true;
    } catch (const std::exception& e) {
        LOG_F(ERROR, "Failed to initialize {}: {}", this->self().ObjectName(), e.what());
        throw;
    }
}

void Buffer::Release() noexcept
{
    if (allocation_ != nullptr) {
        allocation_->Release();
        allocation_ = nullptr;
    }
    resource_->Release();
    resource_ = nullptr;
    should_release_ = false;
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
