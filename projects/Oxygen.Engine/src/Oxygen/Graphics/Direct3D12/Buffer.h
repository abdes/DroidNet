//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Direct3D12/GraphicResource.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

namespace oxygen::graphics {

namespace detail {
    class PerFrameResourceManager;
} // namespace detail

namespace d3d12 {

    class Buffer : public graphics::Buffer {
        using Base = graphics::Buffer;

    public:
        Buffer(const BufferDesc& desc);
        ~Buffer() override;

        OXYGEN_MAKE_NON_COPYABLE(Buffer)
        OXYGEN_MAKE_NON_MOVABLE(Buffer)

        [[nodiscard]] OXYGEN_D3D12_API auto GetDescriptor() const noexcept -> BufferDesc override;

        [[nodiscard]] OXYGEN_D3D12_API auto GetNativeResource() const -> NativeObject override;

        [[nodiscard]] OXYGEN_D3D12_API auto GetResource() const -> ID3D12Resource*;
        [[nodiscard]] OXYGEN_D3D12_API auto Map(size_t offset = 0, size_t size = 0) -> void* override;
        OXYGEN_D3D12_API void UnMap() override;
        OXYGEN_D3D12_API void Update(const void* data, size_t size, size_t offset = 0) override;
        [[nodiscard]] OXYGEN_D3D12_API auto GetSize() const noexcept -> size_t override;
        [[nodiscard]] OXYGEN_D3D12_API auto GetUsage() const noexcept -> BufferUsage override;
        [[nodiscard]] OXYGEN_D3D12_API auto GetMemoryType() const noexcept -> BufferMemory override;
        [[nodiscard]] OXYGEN_D3D12_API auto IsMapped() const noexcept -> bool override;
        OXYGEN_D3D12_API void SetName(std::string_view name) noexcept override;

        // Implementation of the GPU virtual address getter
        [[nodiscard]] OXYGEN_D3D12_API uint64_t GetGPUVirtualAddress() const override;

    protected:
        // --- New view creation methods ---
        [[nodiscard]] auto CreateConstantBufferView(
            const DescriptorHandle& view_handle,
            const BufferRange& range = {}) const -> NativeObject override;

        [[nodiscard]] auto CreateShaderResourceView(
            const DescriptorHandle& view_handle,
            Format format,
            BufferRange range = {},
            uint32_t stride = 0) const -> NativeObject override;

        [[nodiscard]] auto CreateUnorderedAccessView(
            const DescriptorHandle& view_handle,
            Format format,
            BufferRange range = {},
            uint32_t stride = 0) const -> NativeObject override;

    private:
        BufferDesc desc_ {}; // Store the full descriptor
        bool mapped_ { false };
    };

} // namespace d3d12

} // namespace oxygen::graphics::d3d12
