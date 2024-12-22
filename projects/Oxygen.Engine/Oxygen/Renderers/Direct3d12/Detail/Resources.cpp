//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Renderers/Direct3d12/Detail/resources.h"

#include <utility>

#include "oxygen/base/logging.h"
#include "Oxygen/Base/Windows/ComError.h"
#include "Oxygen/Renderers/Direct3d12/Detail/dx12_utils.h"
#include "Oxygen/Renderers/Direct3d12/IDeferredReleaseController.h"
#include "Oxygen/Renderers/Direct3d12/Types.h"

using namespace oxygen::renderer::d3d12::detail;
using  oxygen::renderer::d3d12::Renderer;

void DeferredResourceReleaseTracker::DeferRelease(IUnknown* resource)
{
  DCHECK_NOTNULL_F(resource);
  if (resource == nullptr) return;

  const auto renderer = renderer_.lock();
  if (!renderer) throw std::runtime_error("DeferredRelease not initialized, renderer is not available");

  {
    std::lock_guard lock{ mutex_ };
    deferred_releases_[CurrentFrameIndex()].push_back(resource);
  }

  renderer->RegisterDeferredReleases(
    [this](const size_t frame_index)
    {
      ProcessDeferredReleases(frame_index);
    });
}

void DeferredResourceReleaseTracker::ProcessDeferredReleases(const size_t frame_index)
{
  DCHECK_LE_F(frame_index, kFrameBufferCount);
  DLOG_F(1, "DeferredResourceReleaseTracker::ProcessDeferredRelease for frame index `{}`", frame_index);

  std::lock_guard lock{ mutex_ };
  auto& deferred_releases = deferred_releases_[frame_index];
  for (auto resource : deferred_releases)
  {
    ObjectRelease(resource);
  }
  deferred_releases.clear();
}

void DescriptorHeap::Initialize(
  const size_t capacity,
  bool is_shader_visible,
  DeviceType* device,
  DeferredReleaseControllerPtr renderer)
{
  Release();

  std::lock_guard lock{ mutex_ };
  is_released_ = false;

  DCHECK_NOTNULL_F(device);
  DCHECK_NE_F(0, capacity);
  DCHECK_F(!(is_shader_visible && capacity > D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_2));
  DCHECK_F(!(is_shader_visible && type_ == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER && capacity > D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE));

  if (type_ == D3D12_DESCRIPTOR_HEAP_TYPE_DSV || type_ == D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
  {
    is_shader_visible = false;
  }


  renderer_ = std::move(renderer);

  const D3D12_DESCRIPTOR_HEAP_DESC desc{
    .Type = type_,
    .NumDescriptors = static_cast<UINT>(capacity),
    .Flags = is_shader_visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
    .NodeMask = 0,
  };

  try {
    ThrowOnFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap_)));

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
  }
  catch (const std::exception& e)
  {
    LOG_F(ERROR, "DescriptorHeap initialization failed: {}", e.what());
    Release();
    throw;
  }
}

void DescriptorHeap::Release()
{
  std::lock_guard lock{ mutex_ };
  if (is_released_) return;

  for (const auto& frame_deferred_indices : deferred_release_indices_)
  {
    DCHECK_F(frame_deferred_indices.empty());
  }

  DeferredObjectRelease(heap_);

  free_handles_.reset();
  capacity_ = 0;
  size_ = 0;
  descriptor_size_ = 0;

  is_released_ = true;
}

auto DescriptorHeap::Allocate() -> DescriptorHandle
{
  std::lock_guard lock{ mutex_ };

  DCHECK_NOTNULL_F(heap_);
  CHECK_LT_F(size_, capacity_, "DescriptorHeap::Allocate: heap is full");

  DescriptorHandle handle;
  const size_t index = free_handles_[size_];
  const auto offset = index * descriptor_size_;
  handle.cpu.ptr = cpu_start_.ptr + offset;
  if (IsShaderVisible())
  {
    handle.gpu.ptr = gpu_start_.ptr + offset;
  }
  ++size_;

#if _DEBUG
  handle.heap = this;
  handle.index = index;
#endif

  return handle;
}

void DescriptorHeap::Free(DescriptorHandle& handle)
{
  if (!handle.IsValid()) return;

  std::lock_guard lock{ mutex_ };
  DCHECK_NOTNULL_F(heap_);
  DCHECK_NE_F(0, size_);
  DCHECK_EQ_F(handle.heap, this);
  DCHECK_GE_F(handle.cpu.ptr, cpu_start_.ptr);
  DCHECK_EQ_F(0, (handle.cpu.ptr - cpu_start_.ptr) % descriptor_size_);
  DCHECK_LT_F(handle.index, size_);
  DCHECK_LT_F(handle.index, capacity_);
  DCHECK_LT_F(handle.cpu.ptr, cpu_start_.ptr + descriptor_size_ * capacity_);
  DCHECK_EQ_F(handle.index, (handle.cpu.ptr - cpu_start_.ptr) / descriptor_size_);

  // Remember the index for deferred release
  if (const auto renderer = renderer_.lock())
  {
    const auto frame_index = CurrentFrameIndex();
    deferred_release_indices_[frame_index].push_back(handle.index);
    renderer->RegisterDeferredReleases(
      [this](const size_t current_frame_index)
      {
        ProcessDeferredRelease(current_frame_index);
      });
  }

  handle = {};
}

auto DescriptorHeap::ProcessDeferredRelease(const size_t frame_index) -> void
{
  std::lock_guard lock{ mutex_ };

  DCHECK_LE_F(frame_index, kFrameBufferCount);
  DLOG_F(1, "DescriptorHeap::ProcessDeferredRelease for frame index `{}`", frame_index);

  auto& indices = deferred_release_indices_[frame_index];
  for (const auto index : indices)
  {
    --size_;
    free_handles_[size_] = index;
  }
  indices.clear();
}
