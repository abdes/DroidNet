//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Direct3D12/Resources/GraphicResource.h>

namespace oxygen::graphics {

namespace detail {
    class PerFrameResourceManager;
} // namespace detail

namespace d3d12 {

    class Buffer : public graphics::Buffer {
        using Base = graphics::Buffer;

    public:
        Buffer(
            graphics::detail::PerFrameResourceManager& resource_manager,
            const BufferDesc& desc,
            const void* initial_data = nullptr);
        ~Buffer() override;

        OXYGEN_MAKE_NON_COPYABLE(Buffer);
        OXYGEN_MAKE_NON_MOVABLE(Buffer);

        [[nodiscard]] auto GetResource() const -> ID3D12Resource*;
        [[nodiscard]] auto Map(size_t offset = 0, size_t size = 0) -> void* override;
        void Unmap() override;
        void Update(const void* data, size_t size, size_t offset = 0) override;
        [[nodiscard]] auto GetSize() const noexcept -> size_t override;
        [[nodiscard]] auto GetUsage() const noexcept -> BufferUsage override;
        [[nodiscard]] auto GetMemoryType() const noexcept -> BufferMemory override;
        [[nodiscard]] auto IsMapped() const noexcept -> bool override;
        [[nodiscard]] auto GetDescriptor() const noexcept -> BufferDesc override;
        [[nodiscard]] auto GetNativeResource() const -> NativeObject override;
        void SetName(std::string_view name) noexcept override;

    private:
        void CreateBufferResource(
            graphics::detail::PerFrameResourceManager& resource_manager,
            const BufferDesc& desc,
            const void* initial_data);

        size_t size_ { 0 };
        BufferUsage usage_ { BufferUsage::kNone };
        BufferMemory memory_ { BufferMemory::kDeviceLocal };
        uint32_t stride_ { 0 };
        bool mapped_ { false };
    };

} // namespace d3d12

} // namespace oxygen::graphics::d3d12
