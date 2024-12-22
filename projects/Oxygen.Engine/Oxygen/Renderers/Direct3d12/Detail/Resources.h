//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include <mutex>
#include <utility>
#include <vector>

#include "oxygen/base/macros.h"
#include "Oxygen/Renderers/Common/Types.h"
#include "Oxygen/Renderers/Direct3d12/Types.h"

namespace oxygen::renderer::d3d12::detail {

  class DeferredResourceReleaseTracker
  {
  public:
    static DeferredResourceReleaseTracker& Instance()
    {
      static DeferredResourceReleaseTracker instance;
      return instance;
    }

    void DeferRelease(IUnknown* resource);
    void ProcessDeferredReleases(size_t frame_index);

    void Initialize(DeferredReleaseControllerPtr renderer)
    {
      renderer_ = std::move(renderer);
    }

  private:
    DeferredResourceReleaseTracker() = default;
    ~DeferredResourceReleaseTracker() = default;

    OXYGEN_MAKE_NON_COPYABLE(DeferredResourceReleaseTracker);
    OXYGEN_MAKE_NON_MOVEABLE(DeferredResourceReleaseTracker);

    std::vector<IUnknown*> deferred_releases_[kFrameBufferCount]{};
    DeferredReleaseControllerPtr renderer_;
    std::mutex mutex_;
  };

  class DescriptorHeap;

  inline size_t kInvalidIndex{ std::numeric_limits<size_t>::max() };

  struct DescriptorHandle
  {
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};

    [[nodiscard]] auto IsValid() const -> bool { return cpu.ptr != 0; }
    [[nodiscard]] auto IsShaderVisible() const -> bool { return gpu.ptr != 0; }

  private:
    friend class DescriptorHeap;
    size_t index{ kInvalidIndex };
#ifdef _DEBUG
    DescriptorHeap* heap{ nullptr };
#endif
  };

  class DescriptorHeap
  {
  public:
    explicit DescriptorHeap(const D3D12_DESCRIPTOR_HEAP_TYPE type) : type_{ type }
    {
    }
    ~DescriptorHeap() { Release(); }

    OXYGEN_MAKE_NON_COPYABLE(DescriptorHeap);
    OXYGEN_MAKE_NON_MOVEABLE(DescriptorHeap);

    void Initialize(
      size_t capacity,
      bool is_shader_visible,
      DeviceType* device,
      DeferredReleaseControllerPtr renderer);

    void Release();

    [[nodiscard]] auto Allocate() -> DescriptorHandle;
    void Free(DescriptorHandle& handle);

    void ProcessDeferredRelease(size_t frame_index);

    [[nodiscard]] auto Heap() const -> DescriptorHeapType* { return heap_; }
    [[nodiscard]] auto Size() const -> size_t { return size_; }
    [[nodiscard]] auto Capacity() const -> size_t { return capacity_; }
    [[nodiscard]] auto Type() const -> D3D12_DESCRIPTOR_HEAP_TYPE { return type_; }
    [[nodiscard]] auto CpuStart() const -> D3D12_CPU_DESCRIPTOR_HANDLE { return cpu_start_; }
    [[nodiscard]] auto GpuStart() const -> D3D12_GPU_DESCRIPTOR_HANDLE { return gpu_start_; }
    [[nodiscard]] auto DescriptorSize() const -> size_t { return descriptor_size_; }
    [[nodiscard]] auto IsValid() const -> bool { return heap_ != nullptr; }

    [[nodiscard]] auto IsShaderVisible() const -> bool { return gpu_start_.ptr != 0; }

  private:
    std::mutex mutex_{};
    bool is_released_{ false };

    DescriptorHeapType* heap_{ nullptr };
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_start_{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_start_{};
    size_t capacity_{ 0 };
    size_t size_{ 0 };
    size_t descriptor_size_{ 0 };
    D3D12_DESCRIPTOR_HEAP_TYPE type_{};
    std::unique_ptr<size_t[]> free_handles_{};

    // Remember what indices for handles that are to be freed but still referred
    // to in the corresponding frame slot.
    std::vector<size_t> deferred_release_indices_[kFrameBufferCount]{};
    DeferredReleaseControllerPtr renderer_;
  };
}
