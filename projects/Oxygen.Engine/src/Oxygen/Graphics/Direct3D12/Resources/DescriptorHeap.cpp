//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/StringUtils.h>
#include <Oxygen/Base/Windows/ComError.h>
#include <Oxygen/Graphics/Common/ObjectRelease.h>
#include <Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h>
#include <Oxygen/Graphics/Direct3D12/Resources/DescriptorHeap.h>

using namespace oxygen::windows;
using namespace oxygen::graphics::d3d12::detail;

inline DescriptorHandle::~DescriptorHandle()
{
    DLOG_F(3, "DescriptorHandle::~DescriptorHandle()");
}

inline void DescriptorHandle::Free()
{
    CHECK_NOTNULL_F(allocator);
    DLOG_F(2, "DescriptorHandle::Free() handle index `{}`", index);
    allocator->Free(*this);
}

DescriptorHeap::DescriptorHeap(const InitInfo& init_info)
    : type_(init_info.type)
{
    AddComponent<ObjectMetaData>(init_info.name);

    std::lock_guard lock { mutex_ };
    should_release_ = true;

    DCHECK_NOTNULL_F(init_info.device);
    DCHECK_NE_F(0, init_info.capacity);

    auto is_shader_visible = init_info.is_shader_visible;
    DCHECK_F(!(is_shader_visible && init_info.capacity > D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_2));
    DCHECK_F(!(is_shader_visible && type_ == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER && init_info.capacity > D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE));
    if (type_ == D3D12_DESCRIPTOR_HEAP_TYPE_DSV || type_ == D3D12_DESCRIPTOR_HEAP_TYPE_RTV) {
        is_shader_visible = false;
    }

    const D3D12_DESCRIPTOR_HEAP_DESC desc {
        .Type = type_,
        .NumDescriptors = static_cast<UINT>(init_info.capacity),
        .Flags = is_shader_visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
        .NodeMask = 0,
    };

    try {
        ThrowOnFailed(init_info.device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap_)));
        NameObject(heap_, GetName());

        free_handles_ = std::make_unique<size_t[]>(init_info.capacity);
        capacity_ = init_info.capacity;
        size_ = 0;
        for (size_t i = 0; i < capacity_; ++i) {
            free_handles_[i] = i;
        }
        descriptor_size_ = init_info.device->GetDescriptorHandleIncrementSize(type_);
        cpu_start_ = heap_->GetCPUDescriptorHandleForHeapStart();
        if (is_shader_visible) {
            gpu_start_ = heap_->GetGPUDescriptorHandleForHeapStart();
        }

        LOG_F(INFO, "{} initialized (capacity={})", GetName(), capacity_);
    } catch (const std::exception& ex) {
        LOG_F(ERROR, "{} initialization failed: {}", GetName(), ex.what());
        Release();
        throw;
    }
}

DescriptorHeap::~DescriptorHeap() noexcept
{
    Release();
}

void DescriptorHeap::Release() noexcept
{
    std::lock_guard lock { mutex_ };
    if (!should_release_)
        return;

    ObjectRelease(heap_);

    free_handles_.reset();
    capacity_ = 0;
    size_ = 0;
    descriptor_size_ = 0;

    should_release_ = false;
}

auto DescriptorHeap::Allocate() -> DescriptorHandle
{
    std::lock_guard lock { mutex_ };

    DCHECK_NOTNULL_F(heap_);
    CHECK_LT_F(size_, capacity_, fmt::format("{} is full", GetName()).c_str());

    DescriptorHandle handle(this);
    const size_t index = free_handles_[size_];
    const auto offset = index * descriptor_size_;
    handle.cpu.ptr = cpu_start_.ptr + offset;
    if (IsShaderVisible()) {
        handle.gpu.ptr = gpu_start_.ptr + offset;
    }
    ++size_;

    handle.index = index;

    return handle;
}

void DescriptorHeap::Free(DescriptorHandle& handle)
{
    if (!handle.IsValid())
        return;

    std::lock_guard lock { mutex_ };
    DCHECK_NOTNULL_F(heap_);
    DCHECK_NE_F(0, size_);
    DCHECK_EQ_F(handle.allocator, this);
    DCHECK_GE_F(handle.cpu.ptr, cpu_start_.ptr);
    DCHECK_EQ_F(0, (handle.cpu.ptr - cpu_start_.ptr) % descriptor_size_);

    // TODO: check if this check is really correct
    // DCHECK_LT_F(handle.index, size_);

    DCHECK_LT_F(handle.index, capacity_);
    DCHECK_LT_F(handle.cpu.ptr, cpu_start_.ptr + descriptor_size_ * capacity_);
    DCHECK_EQ_F(handle.index, (handle.cpu.ptr - cpu_start_.ptr) / descriptor_size_);

    --size_;
    free_handles_[size_] = handle.index;
}
