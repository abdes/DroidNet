//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <mutex>
#include <utility>
#include <vector>

#include <d3d12.h>

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Base/Mixin.h"
#include "Oxygen/Base/MixinNamed.h"
#include "Oxygen/Renderers/Common/Types.h"
#include "Oxygen/Renderers/Direct3d12/Types.h"

namespace oxygen::renderer::d3d12::detail {

  inline size_t kInvalidIndex{ std::numeric_limits<size_t>::max() };

  struct DescriptorHandle
  {
    friend class DescriptorHeap;
    friend struct DescriptorHandleDeleter;

    DescriptorHandle() = default;
    ~DescriptorHandle();

    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};

    [[nodiscard]] auto IsValid() const -> bool { return cpu.ptr != 0; }
    [[nodiscard]] auto IsShaderVisible() const -> bool { return gpu.ptr != 0; }

    struct Deleter
    {
      void operator()(DescriptorHandle* handle) const noexcept
      {
        if (handle)
        {
          handle->Free();
          handle->allocator = nullptr;
          delete handle;
        }
      }
    };

  private:
    // Constructor to initialize allocator_
    explicit DescriptorHandle(DescriptorHeap* allocator) noexcept
      : allocator(allocator)
    {
    }
    void Free();

    size_t index{ kInvalidIndex };
    DescriptorHeap* allocator{ nullptr };
  };

  class DescriptorHeap
    : public Mixin< DescriptorHeap, Curry<MixinNamed, const char*>::mixin >
  {
  public:
    //! Forwarding constructor
    template <typename... Args>
    constexpr explicit DescriptorHeap(const D3D12_DESCRIPTOR_HEAP_TYPE type, Args &&...args)
      : type_{ type }, Mixin(static_cast<decltype(args)>(args)...)
    {
    }
    ~DescriptorHeap() { Release(); }

    OXYGEN_MAKE_NON_COPYABLE(DescriptorHeap);
    OXYGEN_MAKE_NON_MOVEABLE(DescriptorHeap);

    void Initialize(size_t capacity, bool is_shader_visible, DeviceType* device);
    void Release();

    [[nodiscard]] auto Allocate() -> DescriptorHandle;
    void Free(DescriptorHandle& handle);

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
    bool should_release_{ false };

    DescriptorHeapType* heap_{ nullptr };
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_start_{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_start_{};
    size_t capacity_{ 0 };
    size_t size_{ 0 };
    size_t descriptor_size_{ 0 };
    D3D12_DESCRIPTOR_HEAP_TYPE type_{};
    std::unique_ptr<size_t[]> free_handles_{};
  };

}  // namespace oxygen::renderer::d3d12::detail
