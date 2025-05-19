//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <ranges>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/Detail/DescriptorHeapSegment.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

namespace oxygen::graphics::detail {

//! Configuration for descriptor allocator initialization.
/*!
 Defines the initial sizes and growth parameters for descriptor heaps.
 Different view types and visibility options can have different allocation
 strategies.
*/
struct BaseDescriptorAllocatorConfig {
    //! Heap mapping strategy (application/backend provided, must outlive allocator construction)
    std::shared_ptr<const DescriptorAllocationStrategy> heap_strategy;
};

//! Base implementation of descriptor allocation and management.
/*!
 Provides common functionality for descriptor allocation, tracking, and recycling
 that can be used by backend-specific implementations. Manages descriptor heap
 segments for different view types and visibility options.

 This class implements the core functionality of the DescriptorAllocator interface,
 but leaves backend-specific operations (like native handle conversion and
 rendering preparation) to derived classes.

 Thread safety is provided through a mutex that protects all allocation and
 release operations.
*/
class BaseDescriptorAllocator : public DescriptorAllocator {
public:
    explicit BaseDescriptorAllocator(BaseDescriptorAllocatorConfig config)
        : config_(std::move(config))
    {
        if (!config_.heap_strategy) {
            static DefaultDescriptorAllocationStrategy default_strategy;
            config_.heap_strategy = std::make_shared<DefaultDescriptorAllocationStrategy>();
        }
        PrecomputeHeapKeys();
        DLOG_F(INFO, "Descriptor Allocator created; {} heaps configured in allocation strategy.",
            heaps_.size());
    }

    ~BaseDescriptorAllocator() override
    {
        // Release all heaps, but do a sanity check to ensure all
        // descriptors have been released.
        for (auto& [key, desc, segments] : heaps_) {
            auto segments_count = segments.size();
            if (segments_count == 0) {
                continue;
            }
            DLOG_F(1, "Cleaning up heap `{}` with {} segment{}",
                key, segments_count, segments_count == 1 ? "" : "s");
            LOG_SCOPE_F(1, "Releasing segments");
            for (const auto& segment : segments) {
                if (!segment->IsEmpty()) {
                    LOG_F(1, "Heap segment has {} descriptors still allocated.",
                         segment->GetAvailableCount());
                }
            }
            segments.clear();
        }
        DLOG_F(INFO, "Descriptor Allocator destroyed.");
    }

    OXYGEN_MAKE_NON_COPYABLE(BaseDescriptorAllocator)
    OXYGEN_DEFAULT_MOVABLE(BaseDescriptorAllocator)

    //! Allocates a descriptor of the specified view type from the specified visibility.
    /*!
     \param view_type The resource view type to allocate.
     \param visibility The memory visibility to allocate from.
     \return A handle to the allocated descriptor.

     Thread-safe implementation that allocates from the appropriate segment
     based on view type and visibility. Creates new segments if needed and
     allowed by the configuration.
    */
    auto Allocate(
        const ResourceViewType view_type,
        const DescriptorVisibility visibility) -> DescriptorHandle override
    {
        std::lock_guard lock(mutex_);
        auto& [_, desc, segments] = heaps_.at(HeapIndex(view_type, visibility));

        // If no segments exist, create initial segment
        if (segments.empty()) {
            const auto capacity = visibility == DescriptorVisibility::kShaderVisible
                ? desc->shader_visible_capacity
                : desc->cpu_visible_capacity;
            if (capacity == 0) {
                throw std::runtime_error("Failed to allocate descriptor: zero capacity");
            }
            // Use the base index from the allocation strategy
            auto& strategy = GetAllocationStrategy();
            const auto base_index = strategy.GetHeapBaseIndex(view_type, visibility);
            auto segment = CreateHeapSegment(capacity, base_index, view_type, visibility);
            if (!segment) {
                throw std::runtime_error("Failed to allocate descriptor: could not create initial segment");
            }
            segments.push_back(std::move(segment));
        }

        // Try to allocate from existing segments (only if not full)
        for (const auto& segment : segments) {
            if (segment->IsFull()) {
                continue;
            }
            if (const uint32_t index = segment->Allocate();
                index != DescriptorHandle::kInvalidIndex) {
                return CreateDescriptorHandle(index, view_type, visibility);
            }
        }

        DCHECK_F(!segments.empty(), "we should have at least one segment");
        DCHECK_NOTNULL_F(desc, "Heap descriptor in heaps_ table should never be null");

        // If we couldn't allocate from existing segments, try to create a new one
        if (desc->allow_growth && segments.size() < (1 + desc->max_growth_iterations)) {
            const auto& last = segments.back();
            const auto base_index = last->GetBaseIndex() + last->GetCapacity();
            const auto capacity = CalculateGrowthCapacity(desc->growth_factor, last->GetCapacity());
            if (auto segment = CreateHeapSegment(capacity, base_index, view_type, visibility)) {
                segments.push_back(std::move(segment));
                if (const auto index = segments.back()->Allocate();
                    index != DescriptorHandle::kInvalidIndex) {
                    return CreateDescriptorHandle(index, view_type, visibility);
                }
            }
        }

        throw std::runtime_error("Failed to allocate descriptor: out of space");
    }

    //! Releases a previously allocated descriptor.
    /*!
     \param handle The handle to release.

     Thread-safe implementation that returns the descriptor to its
     original segment for future reuse.
    */
    void Release(DescriptorHandle& handle) override
    {
        if (!handle.IsValid()) {
            return;
        }

        // This is now safe because Allocate guarantees non-overlapping index ranges.
        std::lock_guard lock(mutex_);
        const auto view_type = handle.GetViewType();
        const auto visibility = handle.GetVisibility();
        const auto index = handle.GetIndex();
        const auto& segments = heaps_[HeapIndex(view_type, visibility)].segments;
        for (const auto& segment : segments) {
            // Only release to the segment that owns the index range.
            const auto base = segment->GetBaseIndex();
            const auto capacity = segment->GetCapacity();
            if (index >= base && index < base + capacity) {
                if (segment->Release(index)) {
                    handle.Invalidate();
                    return;
                }
                break; // If the owning segment fails, don't try others.
            }
        }
        throw std::runtime_error("Failed to release descriptor: not found in any segment");
    }

    //! Gets the number of remaining descriptors for a specific view type and visibility.
    /*!
     \param view_type The resource view type.
     \param visibility The memory visibility.
     \return The number of descriptors still available.
    */
    auto GetRemainingDescriptorsCount(
        const ResourceViewType view_type,
        const DescriptorVisibility visibility) const noexcept -> uint32_t override
    {
        return AbortOnFailed(__func__, [&]() {
            std::lock_guard lock(mutex_);
            uint32_t total = 0;
            const auto& [_, desc, segments] = heaps_.at(HeapIndex(view_type, visibility));
            for (const auto& segment : segments) {
                total += segment->GetAvailableCount();
            }
            DCHECK_NOTNULL_F(desc, "Heap description in the heaps_ table should never be null");
            if (total == 0 && desc->allow_growth) {
                return GetInitialCapacity(view_type, visibility);
            }
            return total;
        });
    }

    //! Checks if this allocator owns the given descriptor handle.
    /*!
     \param handle The descriptor handle to check.
     \return True if this allocator owns the handle, false otherwise.
    */
    [[nodiscard]] auto Contains(const DescriptorHandle& handle) const -> bool override
    {
        if (!handle.IsValid()) {
            return false;
        }

        return AbortOnFailed(__func__, [&]() {
            std::lock_guard lock(mutex_);
            const auto view_type = handle.GetViewType();
            const auto visibility = handle.GetVisibility();
            const auto index = handle.GetIndex();
            const auto& segments = heaps_[HeapIndex(view_type, visibility)].segments;

            return std::ranges::any_of(segments, [&](const auto& segment) {
                const uint32_t base_index = segment->GetBaseIndex();
                const uint32_t capacity = segment->GetCapacity();
                return index >= base_index && index < (base_index + capacity);
            });
        });
    }

    //! Returns the number of allocated descriptors of a specific view type in a
    //! specific visibility.
    /*!
     \param view_type The resource view type.
     \param visibility The memory visibility.
     \return The number of allocated descriptors.
    */
    [[nodiscard]] auto GetAllocatedDescriptorsCount(
        const ResourceViewType view_type,
        const DescriptorVisibility visibility) const -> uint32_t override
    {
        return AbortOnFailed(__func__, [&]() {
            std::lock_guard lock(mutex_);
            uint32_t total = 0;
            for (const auto& segment : heaps_.at(HeapIndex(view_type, visibility)).segments) {
                total += segment->GetAllocatedCount();
            }

            return total;
        });
    }

protected:
    //! Creates a new heap segment for the specified view type and visibility.
    /*!
     \param capacity The capacity of the new segment.
     \param view_type The resource view type for the new segment.
     \param visibility The memory visibility for the new segment.
     \param base_index The base index for the new segment.
     \return A unique_ptr to the created segment, or nullptr on failure.

     This is the main extension point for derived classes. It should:
     1. Calculate the new segment size based on growth policy
     2. Create the backend-specific heap or pool
     3. Return a DescriptorHeapSegment representing the new allocations

     This function is called with the mutex already locked.
    */
    virtual auto CreateHeapSegment(
        uint32_t capacity,
        uint32_t base_index,
        ResourceViewType view_type,
        DescriptorVisibility visibility) -> std::unique_ptr<DescriptorHeapSegment>
        = 0;

    //! Gets the initial capacity for a specific view type and visibility.
    /*!
     \param view_type The resource view type.
     \param visibility The memory visibility.
     \return The initial capacity for new segments.
    */
    auto GetInitialCapacity(
        const ResourceViewType view_type,
        const DescriptorVisibility visibility) const -> uint32_t
    {
        const std::string& heap_key = heaps_.at(HeapIndex(view_type, visibility)).key;
        DCHECK_F(!heap_key.empty(), "Heap key in the heaps_ table should never be empty");
        DCHECK_F(heap_key != "__Unknown__:__Unknown__", "Heap key in the heaps_ table should never be unknown");

        try {
            const auto& desc = config_.heap_strategy->GetHeapDescription(heap_key);
            return (visibility == DescriptorVisibility::kShaderVisible)
                ? desc.shader_visible_capacity
                : desc.cpu_visible_capacity;
        } catch (...) {
            // This should never happen as the keys are pre-computed from the
            // heap allocation strategy, but if it does, return a value that
            // will not allow allocation.
            return 0;
        }
    }

    auto GetAllocationStrategy() const noexcept -> const graphics::DescriptorAllocationStrategy&
    {
        DCHECK_NOTNULL_F(config_.heap_strategy);
        return *config_.heap_strategy;
    }

private:
    /**
     * Calculates the next capacity for heap growth, rounding to the nearest integer and clamping to IndexT max if needed.
     * Logs a warning if the result would overflow IndexT.
     */
    static auto CalculateGrowthCapacity(const float growth_factor, const uint32_t prev_capacity) -> IndexT
    {
        DCHECK_GT_F(growth_factor, 0.0F, "growth factor must be > 0");
        DCHECK_NE_F(prev_capacity, 0U, "previous capacity must be > 0");

        // The new capacity cannot be greater than the max value of IndexT.
        constexpr auto kMax = std::numeric_limits<IndexT>::max();

        const auto result = static_cast<double>(prev_capacity) * static_cast<double>(growth_factor);
        const auto rounded = std::llround(result);

        if (std::cmp_greater(rounded, kMax)) {
            LOG_F(WARNING, "Growth calculation overflow: requested {}, clamping to max {}", rounded, kMax);
            return kMax;
        }
        return static_cast<IndexT>(rounded);
    }

    /**
     * Pre-computes the mapping from (ResourceViewType, DescriptorVisibility) pairs to heap indices.
     *
     * For each possible combination of ResourceViewType and DescriptorVisibility, this method:
     *  - Calls the heap mapping strategy's GetHeapKey to obtain a unique string key for the heap.
     *  - Stores the key in the corresponding HeapInfo struct in the heaps_ array.
     *  - Ensures that each (type, visibility) pair is mapped to a unique and deterministic index.
     *  - This mapping is static for the lifetime of the allocator and guarantees O(1) lookup.
     *
     * The algorithm is reliable because:
     *  - It iterates over all enum values using kMax sentinels, so all valid pairs are covered.
     *  - The heaps_ array is sized to cover all possible pairs, even if some are unused.
     *  - The strategy is always non-null (defaulted if not provided).
     *  - The mapping is deterministic and does not depend on runtime state.
     *
     * This method is called once from the constructor and never again.
     */
    void PrecomputeHeapKeys()
    {
        for (size_t v = 1; v < kNumVisibilities; ++v) {
            for (size_t t = 1; t < kNumResourceViewTypes; ++t) {
                const auto view_type = static_cast<ResourceViewType>(t);
                const auto visibility = static_cast<DescriptorVisibility>(v);
                const size_t idx = HeapIndex(view_type, visibility);
                auto key = config_.heap_strategy->GetHeapKey(view_type, visibility);
                heaps_[idx] = HeapInfo {
                    .key = key,
                    .description = &config_.heap_strategy->GetHeapDescription(key),
                };
            }
        }
    }

    //! Helper to wrap try/catch/abort for exception-unsafe const methods
    template <typename Func>
    static auto AbortOnFailed(const char* func_name, Func&& f) -> decltype(f())
    {
        try {
            return std::forward<Func>(f)();
        } catch (const std::exception& ex) {
            LOG_F(ERROR, "Exception thrown inside {}: {}", func_name, ex.what());
        } catch (...) {
            LOG_F(ERROR, "Unknown exception thrown inside {}", func_name);
        }
        ABORT_F("This is bad programming, probably due to misuse of the "
                "allocator and its heap strategy. Program will terminate!");
    }

    //! Configuration for the allocator.
    BaseDescriptorAllocatorConfig config_;

    static constexpr size_t kNumResourceViewTypes = static_cast<size_t>(ResourceViewType::kMaxResourceViewType);
    static constexpr size_t kNumVisibilities = static_cast<size_t>(DescriptorVisibility::kMaxDescriptorVisibility);
    static constexpr auto HeapIndex(ResourceViewType type, DescriptorVisibility vis) noexcept -> size_t
    {
        // Abort in debug mode if the type or visibility is invalid. This helper
        // method is too frequently used to add extra checks in release mode,
        // and std::array will do bounds checking if the returned index is used
        // to access the heaps_ array.
        DCHECK_F(IsValid(type), "Invalid ResourceViewType: {}",
            static_cast<std::underlying_type_t<ResourceViewType>>(type));
        DCHECK_F(IsValid(vis), "Invalid DescriptorVisibility: {}",
            static_cast<std::underlying_type_t<DescriptorVisibility>>(vis));

        return static_cast<size_t>(vis) * kNumResourceViewTypes + static_cast<size_t>(type);
    }

    //! Helper struct to store heap information.
    struct HeapInfo {
        std::string key { "__Unknown__:__Unknown__" };
        const HeapDescription* description { nullptr };
        std::vector<std::unique_ptr<DescriptorHeapSegment>> segments {};
    };

    //! Precomputed heap information
    std::array<HeapInfo, kNumResourceViewTypes * kNumVisibilities> heaps_;

    //! Thread synchronization mutex.
    mutable std::mutex mutex_;
};

} // namespace oxygen::graphics::detail
