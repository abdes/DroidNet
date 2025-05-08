//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Logging.h"

#include <string>
#include <variant>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/VariantHelpers.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>

namespace oxygen::graphics::detail {

// Barrier description structures - these contain all information needed to
// create barriers but don't represent the actual API-specific barriers

// Memory barrier description
struct MemoryBarrierDesc {
    NativeObject resource;

    auto operator==(const MemoryBarrierDesc& other) const -> bool
    {
        return resource == other.resource;
    }
};

// Buffer barrier description
struct BufferBarrierDesc {
    NativeObject resource;
    ResourceStates before;
    ResourceStates after;

    auto operator==(const BufferBarrierDesc& other) const -> bool
    {
        return resource == other.resource && before == other.before
            && after == other.after;
    }
};

// Texture barrier description
struct TextureBarrierDesc {
    NativeObject resource;
    ResourceStates before;
    ResourceStates after;
    // Could add additional texture-specific fields like mip levels, array slices, etc.

    auto operator==(const TextureBarrierDesc& other) const -> bool
    {
        return resource == other.resource && before == other.before
            && after == other.after;
    }
};

// The barrier descriptor - a variant that can hold any type of barrier description
using BarrierDesc = std::variant<BufferBarrierDesc, TextureBarrierDesc, MemoryBarrierDesc>;

// Abstract Barrier base class - now just a handle to a barrier description
class Barrier final {
public:
    // Constructor with descriptor only (type deduced from descriptor)
    explicit Barrier(BarrierDesc desc)
        : descriptor_(std::move(desc)) // NOLINT(performance-move-const-arg)
    {
    }

    virtual ~Barrier() = default;

    OXYGEN_DEFAULT_COPYABLE(Barrier)
    OXYGEN_DEFAULT_MOVABLE(Barrier)

    // Check if the barrier is a memory barrier
    [[nodiscard]] auto IsMemoryBarrier() const -> bool
    {
        return std::holds_alternative<MemoryBarrierDesc>(descriptor_);
    }

    // Get the barrier descriptor
    [[nodiscard]] auto GetDescriptor() const -> const BarrierDesc& { return descriptor_; }

    // Get the resource for this barrier
    [[nodiscard]] auto GetResource() const -> NativeObject
    {
        return std::visit(
            [](auto&& desc) -> NativeObject { return desc.resource; },
            descriptor_);
    }

    auto GetStateBefore() -> ResourceStates
    {
        return std::visit(
            overloads {
                [](const BufferBarrierDesc& desc) { return desc.before; },
                [](const TextureBarrierDesc& desc) { return desc.before; },
                [](const auto&) -> ResourceStates {
                    ABORT_F("Unhandled barrier descriptor type in Barrier::GetStateBefore");
                },
            },
            descriptor_);
    }

    auto GetStateAfter() -> ResourceStates
    {
        return std::visit(
            overloads {
                [](const BufferBarrierDesc& desc) { return desc.after; },
                [](const TextureBarrierDesc& desc) { return desc.after; },
                [](const auto&) -> ResourceStates {
                    ABORT_F("Unhandled barrier descriptor type in Barrier::GetStateAfter");
                },
            },
            descriptor_);
    }

    void AppendState(ResourceStates state)
    {
        std::visit(
            overloads {
                [state](BufferBarrierDesc& desc) { desc.after |= state; },
                [state](TextureBarrierDesc& desc) { desc.after |= state; },
                [](auto&) {
                    ABORT_F("Unhandled barrier descriptor type in Barrier::AppendState");
                },
            },
            descriptor_);
    }

private:
    // helper type for the visitor
    template <class... Ts>
    struct overloads : Ts... {
        using Ts::operator()...;
    };

    BarrierDesc descriptor_;
};

OXYGEN_GFX_API auto to_string(const Barrier& barrier) -> std::string;

} // namespace oxygen::graphics::detail
