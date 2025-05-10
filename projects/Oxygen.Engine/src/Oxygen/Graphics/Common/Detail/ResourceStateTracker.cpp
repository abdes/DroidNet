//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <ranges>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Detail/ResourceStateTracker.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Texture.h>

using oxygen::graphics::detail::BufferBarrierDesc;
using oxygen::graphics::detail::MemoryBarrierDesc;
using oxygen::graphics::detail::ResourceStateTracker;

auto ResourceStateTracker::GetTrackingInfo(const NativeObject& resource) -> TrackingInfo&
{
    if (const auto it = tracking_.find(resource); it != tracking_.end()) {
        return it->second;
    }
    throw std::runtime_error("Resource not being tracked");
}

void ResourceStateTracker::RequireBufferState(
    const Buffer& buffer,
    const ResourceStates required_state,
    const bool is_permanent)
{
    const NativeObject native_object = buffer.GetNativeResource();
    auto& tracking_info = GetTrackingInfo(native_object);
    DCHECK_F(std::holds_alternative<BufferTrackingInfo>(tracking_info),
        "Resource is not a buffer or not tracked as a buffer");
    auto& tracking = std::get<BufferTrackingInfo>(tracking_info);

    // Now you can use tracking as before, no need for std::visit
    if (tracking.is_permanent) {
        if (tracking.current_state != required_state) {
            throw std::runtime_error("Cannot change state of a resource marked as permanent");
        }
        return;
    }
    if (is_permanent) {
        tracking.is_permanent = true;
    }
    const bool need_transition = tracking.current_state != required_state;
    const bool need_memory_barrier =
        // Requested state includes UnorderedAccess, AND
        ((required_state & ResourceStates::kUnorderedAccess) == ResourceStates::kUnorderedAccess) && (
            // We are auto inserting memory barriers, OR
            tracking.enable_auto_memory_barriers ||
            // memory barriers are manually managed, and this is the first time
            // a transition for UnorderedAccess is requested
            !tracking.first_memory_barrier_inserted);

    if (need_transition) {
        auto merged = false;
        for (auto& pending_barrier : std::ranges::reverse_view(pending_barriers_)) {
            if (pending_barrier.GetResource() == native_object) {
                if (std::holds_alternative<BufferBarrierDesc>(pending_barrier.GetDescriptor())) {
                    pending_barrier.AppendState(required_state);
                    tracking.current_state = pending_barrier.GetStateAfter();
                    merged = true;
                    break;
                }
                if (pending_barrier.IsMemoryBarrier()) {
                    break;
                }
            }
        }
        if (!merged) {
            pending_barriers_.emplace_back(CreateBufferBarrierDesc(
                native_object,
                tracking.current_state,
                required_state));
        }
    } else if (need_memory_barrier) {
        pending_barriers_.emplace_back(MemoryBarrierDesc { native_object });
        tracking.first_memory_barrier_inserted = true;
    }

    tracking.current_state = required_state;
}

void ResourceStateTracker::RequireTextureState(
    const Texture& resource,
    ResourceStates required_state,
    bool is_permanent)
{
}

void ResourceStateTracker::Clear()
{
    pending_barriers_.clear();
    tracking_.clear();
}

void ResourceStateTracker::OnCommandListClosed()
{
    for (auto& [native_object, tracking] : tracking_) {
        // Visit the tracking info using `constexpr` rather than the `Overloads`
        // pattern to avoid code duplication as all supported barrier types
        // share the same condition for determining whether the initial state
        // should be restored.
        std::visit(
            [&]<typename TDescriptor>(TDescriptor& info) {
                if (!info.is_permanent
                    && info.keep_initial_state
                    && info.current_state != info.initial_state) {
                    // Create the appropriate barrier descriptor and add it to
                    // the pending barriers
                    using T = std::decay_t<TDescriptor>;
                    if constexpr (std::is_same_v<T, BufferTrackingInfo>) {
                        pending_barriers_.emplace_back(CreateBufferBarrierDesc(
                            native_object, info.current_state, info.initial_state));
                    } else if constexpr (std::is_same_v<T, TextureTrackingInfo>) {
                        pending_barriers_.emplace_back(CreateTextureBarrierDesc(
                            native_object, info.current_state, info.initial_state));
                    } else {
                        static_assert(always_false_v<T>, "Unsupported barrier type");
                    }
                    // Reset the current state to the initial state
                    info.current_state = info.initial_state;
                }
            },
            tracking);
    }
}

void ResourceStateTracker::OnCommandListSubmitted()
{
    Clear();
}
