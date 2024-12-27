//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Base/StringUtils.h"
#include "Oxygen/Base/Windows/ComError.h"
#include "Oxygen/Renderers/Common/ObjectRelease.h"
#include "Oxygen/Renderers/Direct3d12/Detail/DescriptorHeap.h"
#include "Oxygen/Renderers/Direct3d12/Detail/dx12_utils.h"

using namespace oxygen::windows;
using namespace oxygen::renderer::d3d12::detail;

inline oxygen::renderer::d3d12::detail::DescriptorHandle::~DescriptorHandle()
{
  DLOG_F(3, "DescriptorHandle::~DescriptorHandle()");
}

inline void DescriptorHandle::Free()
{
  CHECK_NOTNULL_F(allocator);
  DLOG_F(2, "DescriptorHandle::Free() handle index `{}`", index);
  allocator->Free(*this);
}

void DescriptorHeap::Initialize(const size_t capacity, bool is_shader_visible, DeviceType* device)
{
  Release();

  std::lock_guard lock{ mutex_ };
  should_release_ = true;

  DCHECK_NOTNULL_F(device);
  DCHECK_NE_F(0, capacity);
  DCHECK_F(!(is_shader_visible && capacity > D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_2));
  DCHECK_F(!(is_shader_visible && type_ == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER && capacity > D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE));

  if (type_ == D3D12_DESCRIPTOR_HEAP_TYPE_DSV || type_ == D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
  {
    is_shader_visible = false;
  }

  const D3D12_DESCRIPTOR_HEAP_DESC desc{
    .Type = type_,
    .NumDescriptors = static_cast<UINT>(capacity),
    .Flags = is_shader_visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
    .NodeMask = 0,
  };

  try {
    ThrowOnFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap_)));
    std::wstring name{};
    string_utils::Utf8ToWide(ObjectName(), name);
    NameObject(heap_, name.data());

    free_handles_ = std::make_unique<size_t[]>(capacity);
    capacity_ = capacity;
    size_ = 0;
    for (size_t i = 0; i < capacity; ++i)
    {
      free_handles_[i] = i;
    }
    descriptor_size_ = device->GetDescriptorHandleIncrementSize(type_);
    cpu_start_ = heap_->GetCPUDescriptorHandleForHeapStart();
    if (is_shader_visible) gpu_start_ = heap_->GetGPUDescriptorHandleForHeapStart();

    LOG_F(INFO, "{} initialized (capacity={})", ObjectName(), capacity_);
  }
  catch (const std::exception& e)
  {
    LOG_F(ERROR, "{} initialization failed: {}", ObjectName(), e.what());
    Release();
    throw;
  }
}

void DescriptorHeap::Release()
{
  std::lock_guard lock{ mutex_ };
  if (!should_release_) return;

  ObjectRelease(heap_);

  DLOG_F(INFO, "{} released (size={})", ObjectName(), size_);

  free_handles_.reset();
  capacity_ = 0;
  size_ = 0;
  descriptor_size_ = 0;

  should_release_ = false;
}

auto DescriptorHeap::Allocate() -> DescriptorHandle
{
  std::lock_guard lock{ mutex_ };

  DCHECK_NOTNULL_F(heap_);
  CHECK_LT_F(size_, capacity_, fmt::format("{} is full", ObjectName()).c_str());

  DescriptorHandle handle(this);
  const size_t index = free_handles_[size_];
  const auto offset = index * descriptor_size_;
  handle.cpu.ptr = cpu_start_.ptr + offset;
  if (IsShaderVisible())
  {
    handle.gpu.ptr = gpu_start_.ptr + offset;
  }
  ++size_;

  handle.index = index;

  return handle;
}

void DescriptorHeap::Free(DescriptorHandle& handle)
{
  if (!handle.IsValid()) return;

  std::lock_guard lock{ mutex_ };
  DCHECK_NOTNULL_F(heap_);
  DCHECK_NE_F(0, size_);
  DCHECK_EQ_F(handle.allocator, this);
  DCHECK_GE_F(handle.cpu.ptr, cpu_start_.ptr);
  DCHECK_EQ_F(0, (handle.cpu.ptr - cpu_start_.ptr) % descriptor_size_);

  // TODO: check if this check is really correct
  //DCHECK_LT_F(handle.index, size_);

  DCHECK_LT_F(handle.index, capacity_);
  DCHECK_LT_F(handle.cpu.ptr, cpu_start_.ptr + descriptor_size_ * capacity_);
  DCHECK_EQ_F(handle.index, (handle.cpu.ptr - cpu_start_.ptr) / descriptor_size_);

  --size_;
  free_handles_[size_] = handle.index;
}
