//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

// ReSharper disable once CppInconsistentNaming
namespace D3D12MA {
class Allocator;
} // namespace D3D12MA

namespace oxygen::graphics {
class DescriptorHandle;

namespace d3d12 {
  class Graphics;

  class Buffer : public graphics::Buffer {
    using Base = graphics::Buffer;

  public:
    Buffer(BufferDesc desc, const Graphics* gfx);
    ~Buffer() override;

    OXYGEN_MAKE_NON_COPYABLE(Buffer)
    OXYGEN_MAKE_NON_MOVABLE(Buffer)

    OXGN_D3D12_NDAPI auto GetDescriptor() const noexcept -> BufferDesc override;

    OXGN_D3D12_NDAPI auto GetNativeResource() const -> NativeResource override;

    OXGN_D3D12_NDAPI auto GetResource() const -> ID3D12Resource*;
    OXGN_D3D12_API auto Update(const void* data, size_t size, size_t offset = 0)
      -> void override;
    OXGN_D3D12_NDAPI auto GetSize() const noexcept -> size_t override;
    OXGN_D3D12_NDAPI auto GetUsage() const noexcept -> BufferUsage override;
    OXGN_D3D12_NDAPI auto GetMemoryType() const noexcept
      -> BufferMemory override;
    OXGN_D3D12_NDAPI auto IsMapped() const noexcept -> bool override;
    OXGN_D3D12_API auto SetName(std::string_view name) noexcept
      -> void override;

    // Implementation of the GPU virtual address getter
    OXGN_D3D12_NDAPI auto GetGPUVirtualAddress() const -> uint64_t override;

  protected:
    OXGN_D3D12_NDAPI auto DoMap(size_t offset = 0, size_t size = 0)
      -> void* override;
    OXGN_D3D12_API auto DoUnMap() noexcept -> void override;

    // --- New view creation methods ---
    [[nodiscard]] auto CreateConstantBufferView(
      const DescriptorHandle& view_handle, const BufferRange& range = {}) const
      -> NativeView override;

    [[nodiscard]] auto CreateShaderResourceView(
      const DescriptorHandle& view_handle, Format format,
      BufferRange range = {}, uint32_t stride = 0) const -> NativeView override;

    [[nodiscard]] auto CreateUnorderedAccessView(
      const DescriptorHandle& view_handle, Format format,
      BufferRange range = {}, uint32_t stride = 0) const -> NativeView override;

  private:
    auto CurrentDevice() const -> dx::IDevice*;
    auto MemoryAllocator() const -> D3D12MA::Allocator*;

    const Graphics* gfx_ { nullptr };
    BufferDesc desc_ {}; // Store the full descriptor
    bool mapped_ { false };
  };

} // namespace d3d12

} // namespace oxygen::graphics::d3d12
