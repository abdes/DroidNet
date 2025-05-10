//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <variant>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/VariantHelpers.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>


namespace oxygen::graphics::detail {

//! Barrier description for memory operations synchronization.
/*!
 Memory barriers ensure visibility of memory operations across the GPU pipeline
 without requiring explicit state transitions.
*/
struct MemoryBarrierDesc {
    NativeObject resource;

    auto operator==(const MemoryBarrierDesc& other) const -> bool
    {
        return resource == other.resource;
    }
};

//! Barrier description for buffer state transitions.
/*!
 Buffer barriers ensure proper synchronization when a buffer's usage changes
 between different GPU operations (e.g., from vertex buffer to UAV).
*/
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

//! Barrier description for texture state transitions.
/*!
 Texture barriers ensure proper synchronization when a texture's usage changes
 between different GPU operations (rendering, sampling, copying, etc).
*/
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

//! A variant that can hold any type of barrier description
using BarrierDesc = std::variant<BufferBarrierDesc, TextureBarrierDesc, MemoryBarrierDesc>;

//! Unified interface for all types of resource barriers in the graphics system.
/*!
 A barrier describes a resource state transition or synchronization point for
 GPU operations. This class provides a type-safe wrapper around the different
 barrier descriptors (buffer, texture, and memory) and utility methods to access
 their properties.
*/
class Barrier final {
public:
    // Constructor with descriptor only (type deduced from descriptor)
    explicit Barrier(BarrierDesc desc)
        : descriptor_(std::move(desc)) // NOLINT(performance-move-const-arg)
    {
    }

    ~Barrier() = default;

    OXYGEN_DEFAULT_COPYABLE(Barrier)
    OXYGEN_DEFAULT_MOVABLE(Barrier)

    [[nodiscard]] auto IsMemoryBarrier() const -> bool
    {
        return std::holds_alternative<MemoryBarrierDesc>(descriptor_);
    }

    [[nodiscard]] auto GetDescriptor() const -> const BarrierDesc&
    {
        return descriptor_;
    }

    [[nodiscard]] auto GetResource() const -> NativeObject
    {
        return std::visit(
            [](auto&& desc) -> NativeObject { return desc.resource; },
            descriptor_);
    }

    auto GetStateBefore() -> ResourceStates
    {
        return std::visit(
            Overloads {
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
            Overloads {
                [](const BufferBarrierDesc& desc) { return desc.after; },
                [](const TextureBarrierDesc& desc) { return desc.after; },
                [](const auto&) -> ResourceStates {
                    ABORT_F("Unhandled barrier descriptor type in Barrier::GetStateAfter");
                },
            },
            descriptor_);
    }

    //! Append the provided state to the barrier's 'after' state.
    /*!
     This method is used to accumulate multiple states for a resource in a
     single barrier, reducing the number of barriers needed in a command list.
    */
    void AppendState(ResourceStates state)
    {
        std::visit(
            Overloads {
                [state](BufferBarrierDesc& desc) { desc.after |= state; },
                [state](TextureBarrierDesc& desc) { desc.after |= state; },
                [](auto&) {
                    ABORT_F("Unhandled barrier descriptor type in Barrier::AppendState");
                },
            },
            descriptor_);
    }

private:
    BarrierDesc descriptor_;
};

OXYGEN_GFX_API auto to_string(const Barrier& barrier) -> std::string;

} // namespace oxygen::graphics::detail
