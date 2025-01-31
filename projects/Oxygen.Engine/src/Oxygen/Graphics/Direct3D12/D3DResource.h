//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Graphics/Common/Types/ResourceAccessMode.h"

namespace oxygen::graphics::d3d12 {

enum class ResourceType : uint8_t {
    kTexture,
    kBuffer,

    kMax,
};

//! Common description of a graphics resource (buffer or texture)
struct CommonResourceDesc {
    //// memory block to allocate the buffer from
    //// if not provided, renderer will automatically manage memory for the resource
    // MemoryBlockPtr memoryBlock;
    // uint64_t memoryBlockOffset = 0u;

    // optional debug name
    const char* debug_name = nullptr;
};

class D3DResource {
public:
    virtual ~D3DResource() noexcept = default;

    OXYGEN_MAKE_NON_COPYABLE(D3DResource);
    OXYGEN_DEFAULT_MOVABLE(D3DResource);

    [[nodiscard]] virtual auto GetResource() const -> ID3D12Resource* = 0;
    [[nodiscard]] virtual auto GetMode() const -> ResourceAccessMode { return mode_; }

protected:
    D3DResource() = default;

    ResourceAccessMode mode_ { ResourceAccessMode::kImmutable };
};

} // namespace oxygen::graphics::d3d12
