//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Mixin.h>
#include <Oxygen/Base/MixinDisposable.h>
#include <Oxygen/Base/MixinInitialize.h>
#include <Oxygen/Base/MixinNamed.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Direct3d12/D3D12MemAlloc.h>
#include <Oxygen/Graphics/Direct3d12/D3DResource.h>
#include <Oxygen/Graphics/Direct3d12/Graphics.h>

namespace oxygen::graphics::d3d12 {

/**
 * Buffer's usage flags. The buffer usage must be determined upon creation.
 */
enum class BufferUsageFlags : uint8_t {
    kNone = OXYGEN_FLAG(0),

    kIndexBuffer = OXYGEN_FLAG(1),
    kVertexBuffer = OXYGEN_FLAG(2),
    kConstantBuffer = OXYGEN_FLAG(3),
    kReadonlyStruct = OXYGEN_FLAG(4),
    kWritableStruct = OXYGEN_FLAG(5),
    kReadonlyBuffer = OXYGEN_FLAG(6),
    kWritableBuffer = OXYGEN_FLAG(7),
};
OXYGEN_DEFINE_FLAGS_OPERATORS(BufferUsageFlags);

struct BufferDesc : public CommonResourceDesc {
    ResourceAccessMode mode { ResourceAccessMode::kImmutable };
    BufferUsageFlags usage { BufferUsageFlags::kNone };
    uint32_t size { 0u };
    uint32_t struct_size = { 0u };
};

struct BufferInitInfo {
    D3D12MA::ALLOCATION_DESC alloc_desc;
    D3D12_RESOURCE_DESC resource_desc;
    D3D12_RESOURCE_STATES initial_state;
    uint64_t size_in_bytes;
};

class Buffer : public graphics::Buffer, public D3DResource {
public:
    Buffer()
        : D3DResource()
    {
    }
    ~Buffer() override;

    OXYGEN_MAKE_NON_COPYABLE(Buffer);
    OXYGEN_MAKE_NON_MOVABLE(Buffer);

    [[nodiscard]] auto GetResource() const -> ID3D12Resource* override { return resource_; }

    [[nodiscard]] auto GetSize() const -> size_t { return size_; }

    void Initialize(const BufferInitInfo& init_info);
    void Release() noexcept override;

    void Bind() override { };
    void* Map() override;
    void Unmap() override;

private:
    template <typename Base>
    friend class MixinDisposable; //< Allow access to OnRelease.

    D3D12MA::Allocation* allocation_ { nullptr };
    ID3D12Resource* resource_ { nullptr };
    size_t size_ { 0 };
    bool should_release_ { false };
};

} // namespace oxygen::graphics::d3d12
