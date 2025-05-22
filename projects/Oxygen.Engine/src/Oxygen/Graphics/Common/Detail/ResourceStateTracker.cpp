//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <ranges>

#include <Oxygen/Base/AlwaysFalse.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Detail/ResourceStateTracker.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Texture.h>

using oxygen::graphics::NativeObject;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::detail::BufferBarrierDesc;
using oxygen::graphics::detail::MemoryBarrierDesc;
using oxygen::graphics::detail::ResourceStateTracker;
using oxygen::graphics::detail::TextureBarrierDesc;

namespace {

// Create a barrier descriptor for buffer resources
auto CreateBufferBarrierDesc(
    const NativeObject& native_object,
    const ResourceStates before,
    const ResourceStates after) -> BufferBarrierDesc
{
    return BufferBarrierDesc { .resource = native_object, .before = before, .after = after };
    // TODO: Could add buffer-specific fields here  or keep a reference to
    // the buffer object itself
}

// Create a barrier descriptor for texture resources
auto CreateTextureBarrierDesc(
    const NativeObject& native_object,
    const ResourceStates before,
    const ResourceStates after) -> TextureBarrierDesc
{
    return TextureBarrierDesc { .resource = native_object, .before = before, .after = after };
    // TODO: Could add texture-specific fields here (mip levels, array
    // slices, etc.) or keep a reference to the texture object itself
}

} // namespace

auto ResourceStateTracker::HandlePermanentState(
    const BasicTrackingInfo& tracking,
    ResourceStates required_state,
    const char* resource_type_name) -> bool
{
    if (tracking.is_permanent) {
        if (tracking.current_state != required_state) {
            LOG_F(ERROR,
                "Attempt to change the permanent state of a {} resource from {} to {}.",
                resource_type_name,
                nostd::to_string(tracking.current_state),
                nostd::to_string(required_state));
            throw std::runtime_error("Cannot change state of a resource which was "
                                     "previously transitioned to a permanent state");
        }
        return true;
    }
    return false;
}

template <typename BarrierDescType>
auto ResourceStateTracker::TryMergeWithExistingTransition(
    const NativeObject& native_object,
    ResourceStates& current_state,
    const ResourceStates required_state) -> bool
{
    for (auto& pending_barrier : std::ranges::reverse_view(pending_barriers_)) {
        if (pending_barrier.GetResource() == native_object) {
            if (std::holds_alternative<BarrierDescType>(pending_barrier.GetDescriptor())) {
                pending_barrier.AppendState(required_state);
                current_state = pending_barrier.GetStateAfter();
                DLOG_F(4, "Merged with existing transition: {} -> {}",
                    nostd::to_string(pending_barrier.GetStateBefore()),
                    nostd::to_string(pending_barrier.GetStateAfter()));
                return true; // Successfully merged
            }
            if (pending_barrier.IsMemoryBarrier()) {
                // Stop merging if a memory barrier for this resource is encountered
                break;
            }
        }
    }
    return false; // Not merged
}

// Explicit instantiations for the template specializations we need
template auto ResourceStateTracker::TryMergeWithExistingTransition<BufferBarrierDesc>(
    const NativeObject& native_object,
    ResourceStates& current_state,
    const ResourceStates required_state) -> bool;

template auto ResourceStateTracker::TryMergeWithExistingTransition<TextureBarrierDesc>(
    const NativeObject& native_object,
    ResourceStates& current_state,
    const ResourceStates required_state) -> bool;

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
    LOG_F(4, "buffer: require state 0x{:X} = {} {}",
        reinterpret_cast<uintptr_t>(native_object.AsPointer<void>()),
        nostd::to_string(required_state), is_permanent ? " (permanent)" : "");
    auto& tracking_info = GetTrackingInfo(native_object);
    DCHECK_F(std::holds_alternative<BufferTrackingInfo>(tracking_info),
        "Resource is not a buffer or not tracked as a buffer");
    auto& tracking = std::get<BufferTrackingInfo>(tracking_info);

    if (HandlePermanentState(tracking, required_state, "buffer")) {
        return;
    }

    if (is_permanent) {
        tracking.is_permanent = true;
    }

    const bool need_transition = tracking.NeedsTransition(required_state);
    const bool need_memory_barrier = tracking.NeedsMemoryBarrier(required_state);

    if (need_transition) {
        if (!TryMergeWithExistingTransition<BufferBarrierDesc>(
                native_object, tracking.current_state, required_state)) {
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
    const Texture& texture,
    ResourceStates required_state,
    bool is_permanent)
{
    const NativeObject native_object = texture.GetNativeResource();

    LOG_F(4, "texture: require state 0x{:X} = {} {}",
        reinterpret_cast<uintptr_t>(native_object.AsPointer<void>()),
        nostd::to_string(required_state), is_permanent ? " (permanent)" : "");

    auto& tracking_info = GetTrackingInfo(native_object);
    DCHECK_F(std::holds_alternative<TextureTrackingInfo>(tracking_info),
        "Resource is not a texture or not tracked as a texture");
    auto& tracking = std::get<TextureTrackingInfo>(tracking_info);

    if (HandlePermanentState(tracking, required_state, "texture")) {
        return;
    }

    if (is_permanent) {
        tracking.is_permanent = true;
    }

    const bool need_transition = tracking.NeedsTransition(required_state);
    const bool need_memory_barrier = tracking.NeedsMemoryBarrier(required_state);

    if (need_transition) {
        if (!TryMergeWithExistingTransition<TextureBarrierDesc>(
                native_object, tracking.current_state, required_state)) {
            pending_barriers_.emplace_back(CreateTextureBarrierDesc(
                native_object,
                tracking.current_state,
                required_state));
        }
    } else if (need_memory_barrier) {
        // This case handles when the state is not changing (e.g., already UAV)
        // but a memory barrier is needed for synchronization between UAV operations.
        pending_barriers_.emplace_back(MemoryBarrierDesc { native_object });
        tracking.first_memory_barrier_inserted = true;
    }

    // Update the tracked current state to the newly required state.
    // If merged, pending_barrier.GetStateAfter() would be this required_state.
    // If a new transition was added, its 'after' state is this required_state.
    // If only a memory barrier was added, the state itself didn't change, but this reaffirms it.
    tracking.current_state = required_state;
}

void ResourceStateTracker::Clear()
{
    LOG_F(4, " => clearing all tracking");
    if (!pending_barriers_.empty()) {
        pending_barriers_.clear();
    }
    tracking_.clear();
}

void ResourceStateTracker::ClearPendingBarriers()
{
    LOG_F(4, "clearing pending barriers");
    pending_barriers_.clear();
}

void ResourceStateTracker::OnCommandListClosed()
{
    LOG_F(4, "cmd list closed => restore initial states if needed");
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
    LOG_F(4, "cmd list submitted");
    Clear();
}
