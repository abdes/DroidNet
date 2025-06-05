//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <string>
#include <utility>

#include <Oxygen/Scene/Types/SceneFlagEnum.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene {

//! Bit positions within the 5-bit flag layout.
/*!
 Each flag uses a 5-bit layout to store all necessary state information:
 - Bit 0: Effective value (final resolved state)
 - Bit 1: Inheritance flag (whether value comes from parent)
 - Bit 2: Pending value (staged value for next update)
 - Bit 3: Dirty flag (requires processing in update cycle)
 - Bit 4: Previous value (for transition detection)
*/
enum class FlagBitPosition : uint8_t {
    kEffectiveValue = 0, //!< Final resolved value (1 = true, 0 = false)
    kInheritance = 1, //!< Whether value is inherited from parent
    kPendingValue = 2, //!< Pending value to become effective after update
    kDirty = 3, //!< Needs update in scene update pass
    kPreviousValue = 4 //!< Previous effective value for transition detection
};

//! Single flag state wrapper with 5-bit layout for scene graph operations.
/*!
 SceneFlag encapsulates the state of a single boolean flag in the scene graph
 with support for inheritance, deferred updates, and transition tracking.

 The 5-bit layout stores:
 - Effective value: Current resolved boolean state
 - Inheritance flag: Whether this flag inherits from parent node
 - Pending value: Staged value that becomes effective after ProcessDirty()
 - Dirty flag: Indicates the flag needs processing in the update cycle
 - Previous value: Previous effective value for transition detection

 \note All operations are constexpr and noexcept for performance-critical code.
*/
class SceneFlag {
public:
    //! Default constructor initializes all bits to 0.
    /*!
     Creates a flag with:
     - Effective value: false
     - Not inherited
     - Pending value: false
     - Not dirty
     - Previous value: false
    */
    constexpr SceneFlag() noexcept = default;

    //! Explicit constructor from raw bit pattern.
    /*!
     \param bits Raw 5-bit pattern to initialize the flag state.
     \note Only the lower 5 bits are used; upper bits are masked out.
    */
    constexpr explicit SceneFlag(const std::uint8_t bits) noexcept
        : bits_(bits & 0b11111)
    {
    } //=== Bit Access Methods ===---------------------------------------------//

    //! Get the effective (final resolved) value bit.
    [[nodiscard]] constexpr auto GetEffectiveValueBit() const noexcept -> bool
    {
        return (bits_ & 0b00001) != 0;
    }

    //! Set the effective value bit.
    constexpr auto SetEffectiveValueBit(const bool value) noexcept -> SceneFlag&
    {
        bits_ = (bits_ & ~0b00001) | (value ? 0b00001 : 0);
        return *this;
    }

    //! Check if flag inherits its value from parent.
    [[nodiscard]] constexpr auto GetInheritedBit() const noexcept -> bool
    {
        return (bits_ & 0b00010) != 0;
    }

    //! Set whether flag inherits its value from parent.
    constexpr auto SetInheritedBit(const bool value) noexcept -> SceneFlag&
    {
        bits_ = (bits_ & ~0b00010) | (value ? 0b00010 : 0);
        return *this;
    }

    //! Get the pending value (to become effective after update).
    [[nodiscard]] constexpr auto GetPendingValueBit() const noexcept -> bool
    {
        return (bits_ & 0b00100) != 0;
    }

    //! Set the pending value.
    constexpr auto SetPendingValueBit(const bool value) noexcept -> SceneFlag&
    {
        bits_ = (bits_ & ~0b00100) | (value ? 0b00100 : 0);
        return *this;
    }

    //! Check if flag is dirty (needs processing).
    [[nodiscard]] constexpr auto GetDirtyBit() const noexcept -> bool
    {
        return (bits_ & 0b01000) != 0;
    }

    //! Set dirty state for deferred processing.
    constexpr auto SetDirtyBit(const bool value) noexcept -> SceneFlag&
    {
        bits_ = (bits_ & ~0b01000) | (value ? 0b01000 : 0);
        return *this;
    }

    //! Get previous value (for transition detection).
    [[nodiscard]] constexpr auto GetPreviousValueBit() const noexcept -> bool
    {
        return (bits_ & 0b10000) != 0;
    }

    //! Set previous value for transition detection.
    constexpr auto SetPreviousValueBit(const bool value) noexcept -> SceneFlag&
    {
        bits_ = (bits_ & ~0b10000) | (value ? 0b10000 : 0);
        return *this;
    }

    //=== High-Level Operations ===-------------------------------------------//

    //! Get the current effective (resolved) value.
    [[nodiscard]] constexpr auto GetEffectiveValue() const noexcept -> bool
    {
        return GetEffectiveValueBit();
    }

    //! Get the pending value that will become effective after update.
    [[nodiscard]] constexpr auto GetPendingValue() const noexcept -> bool
    {
        return GetPendingValueBit();
    }

    //! Get the previous effective value before last update.
    [[nodiscard]] constexpr auto GetPreviousValue() const noexcept -> bool
    {
        return GetPreviousValueBit();
    }

    //! Check if flag needs processing in update cycle.
    [[nodiscard]] constexpr auto IsDirty() const noexcept
    {
        return GetDirtyBit();
    }

    //! Check if flag inherits value from parent node.
    [[nodiscard]] constexpr auto IsInherited() const noexcept
    {
        return GetInheritedBit();
    }

    //! Set a local value (overrides inheritance).
    /*!
     Sets a local value and disables inheritance. Implements optimization to
     avoid unnecessary dirty marking when the value doesn't change.
    */
    constexpr auto SetLocalValue(bool value) noexcept -> SceneFlag&
    {
        // Always disable inheritance if a local value is set
        SetInheritedBit(false);

        // If we already have a pending change, we need to check if the new
        // change is redundant or if it reverts the pending change.
        if (IsDirty()) {
            if (GetPendingValueBit() == value) {
                return *this;
            }

            // Resetting the pending value to be the same as the effective value,
            // means reverting a pending change.
            if (GetEffectiveValueBit() == value) {
                SetPendingValueBit(value);
                SetDirtyBit(false); // No change, no need to mark dirty
                return *this;
            }
        }

        SetPendingValueBit(value);
        SetDirtyBit(true);
        return *this;
    }

    //! Enable or disable inheritance from parent node.
    /*!
     When inheritance is enabled, the flag's effective value will be updated
     from the parent during the scene update cycle.
    */
    constexpr auto SetInherited(const bool state) noexcept -> SceneFlag&
    {
        // Always enable inheritance if this is called
        SetInheritedBit(state);

        // Dirty flag management is similar to SetLocalValue, but we do not
        // change the pending value here as it is inherited and will be updated
        // during the next scene update cycle.
        SetDirtyBit(true);
        return *this;
    }

    //! Update flag value from parent node (for inherited flags only).
    /*!
     This method should only be called for flags that are marked as inherited.
     It updates the pending value from the parent's effective value and marks
     the flag as dirty if the value changes.

     \note This method is typically called during the scene update cycle.
    */
    OXYGEN_SCENE_API auto UpdateValueFromParent(bool value) noexcept -> SceneFlag&;

    //! Apply pending value to effective value if dirty.
    /*!
     Processes a dirty flag by:
     1. Storing current effective value as previous value.
     2. Moving pending value to effective value.
     3. Clearing dirty flag.

     \note Called during the scene update cycle.

     \return true if the flag was successfully processed, false if it was not
             dirty or applying the effective value failed.
    */
    OXYGEN_SCENE_API auto ProcessDirty() noexcept -> bool;

    //=== Raw Data Access ===-------------------------------------------------//

    //! Get raw bit pattern.
    [[nodiscard]] constexpr auto GetRaw() const noexcept -> std::uint8_t
    {
        return bits_;
    }

    //! Set raw bit pattern.
    /*!
     \note Only lower 5 bits are used; upper bits are masked out.
    */
    constexpr auto SetRaw(const std::uint8_t bits) noexcept -> SceneFlag&
    {
        bits_ = bits & 0b11111;
        return *this;
    }

    //=== Comparison Operators ===--------------------------------------------//

    //! Bitwise equality comparison (compares all bits).
    [[nodiscard]] constexpr auto operator==(const SceneFlag& other) const noexcept -> bool
    {
        return bits_ == other.bits_;
    }

    //! Bitwise inequality comparison.
    [[nodiscard]] constexpr auto operator!=(const SceneFlag& other) const noexcept -> bool
    {
        return bits_ != other.bits_;
    }

    //! Semantic equality comparison based on effective values.
    /*!
     Returns true only if both flags are not dirty and have the same effective
     value. Returns false if either flag is dirty (unstable state).
    */
    [[nodiscard]] constexpr auto EffectiveEquals(const SceneFlag& other) const noexcept
    {
        // If either flag is dirty, they cannot be considered equal
        if (IsDirty() || other.IsDirty()) {
            return false;
        }
        // Compare only effective values when both are stable
        return GetEffectiveValue() == other.GetEffectiveValue();
    }

    //! Semantic inequality comparison based on effective values.
    [[nodiscard]] constexpr auto EffectiveNotEquals(const SceneFlag& other) const noexcept
    {
        return !EffectiveEquals(other);
    }

private:
    std::uint8_t bits_ = 0; ///< 5 bits used for flag state, 3 bits reserved
};
OXYGEN_SCENE_API auto constexpr to_string(SceneFlag value) noexcept -> std::string;

//! Template-based scene graph flags container with inheritance support.
/*!
 Provides a compact storage system for flags with 5-bit state per flag. Each
 flag maintains:
 - Effective value (final resolved state)
 - Inheritance state (whether it inherits from parent)
 - Pending value (value to become effective after update)
 - Dirty flag (for batched processing)
 - Previous value (for transition detection)

 Supports up to 12 flags in a 64-bit storage with compile-time bounds checking.

 \tparam FlagEnum Enum type that must satisfy SceneFlagEnum concept.
*/
template <SceneFlagEnum FlagEnum>
class SceneFlags {
public:
    using flag_type = FlagEnum;
    using flag_value_type = SceneFlag;
    using storage_type = std::uint64_t;
    static constexpr auto flag_count = static_cast<std::size_t>(FlagEnum::kCount);
    static constexpr auto bits_per_flag = 5;
    static constexpr auto flag_mask = 0b11111;

    //=== Forward Iterator Declaration ===------------------------------------//

    class const_iterator;
    friend class const_iterator;

    //=== Construction and Assignment ===-------------------------------------//

    //! Default constructor initializes all flags to zeros.
    constexpr SceneFlags() noexcept = default;

    //! Copy constructor.
    constexpr SceneFlags(const SceneFlags&) noexcept = default;

    //! Move constructor.
    constexpr SceneFlags(SceneFlags&&) noexcept = default;

    //! Copy assignment operator.
    constexpr auto operator=(const SceneFlags&) noexcept -> SceneFlags& = default;

    //! Move assignment operator.
    constexpr auto operator=(SceneFlags&&) noexcept -> SceneFlags& = default;

    //! Destructor.
    ~SceneFlags() = default;

    //=== Flag Access Methods ===---------------------------------------------//

    //! Set the full flag state.
    constexpr auto SetFlag(FlagEnum flag, const SceneFlag& value) noexcept -> SceneFlags&
    {
        SetFlagBits(static_cast<std::size_t>(flag), value.GetRaw());
        return *this;
    }

    //=== High-Level Flag Operations ===--------------------------------------//

    //! Get effective value for specified flag.
    [[nodiscard]] constexpr auto GetEffectiveValue(FlagEnum flag) const noexcept
    {
        return GetFlag(flag).GetEffectiveValue();
    }

    //! Get pending value for specified flag.
    [[nodiscard]] constexpr auto GetPendingValue(FlagEnum flag) const noexcept
    {
        return GetFlag(flag).GetPendingValue();
    }

    //! Get previous value for specified flag.
    [[nodiscard]] constexpr auto GetPreviousValue(FlagEnum flag) const noexcept
    {
        return GetFlag(flag).GetPreviousValue();
    }

    //! Check if specified flag is dirty.
    [[nodiscard]] constexpr auto IsDirty(FlagEnum flag) const noexcept
    {
        return GetFlag(flag).IsDirty();
    }

    //! Check if specified flag inherits from parent.
    [[nodiscard]] constexpr auto IsInherited(FlagEnum flag) const noexcept
    {
        return GetFlag(flag).IsInherited();
    }

    //! Set a local value (overrides inheritance).
    constexpr auto SetLocalValue(FlagEnum flag, bool value) noexcept -> SceneFlags&
    {
        auto flag_state = GetFlag(flag);
        flag_state.SetLocalValue(value);
        return SetFlag(flag, flag_state);
    }

    //! Enable inheritance from parent.
    constexpr auto SetInherited(FlagEnum flag, bool state) noexcept -> SceneFlags&
    {
        auto flag_state = GetFlag(flag);
        flag_state.SetInherited(state);
        return SetFlag(flag, flag_state);
    }

    //! Update a flag from parent (if it's in inherit mode).
    /*!
     This method should only be called for flags that are marked as inherited.
     It updates the pending value from the parent's effective value and marks
     the flag as dirty if the value changes.

     \note This method is typically called during the scene update cycle.
    */
    constexpr auto UpdateValueFromParent(
        FlagEnum flag, const SceneFlags& parent) noexcept -> SceneFlags&
    {
        auto flag_state = GetFlag(flag);
        flag_state.UpdateValueFromParent(parent.GetEffectiveValue(flag));
        return SetFlag(flag, flag_state);
    }

    //! Process a single dirty flag.
    /*!
     Expects the flag to be dirty. Processes the flag and returns true if
     the flag was successfully processed.

     \note Called during the scene update cycle.

     \return true if the flag was successfully processed, false if it was not
             dirty or applying the effective value failed.
    */
    constexpr auto ProcessDirtyFlag(FlagEnum flag) noexcept -> bool
    {
        auto flag_state = GetFlag(flag);

        if (!flag_state.IsDirty()) {
            return false; // Nothing to process
        }

        if (flag_state.ProcessDirty()) {
            SetFlag(flag, flag_state);
            return true;
        }
        return false;
    }

    //=== Bulk Operations ===------------------------------------------------//

    //! Reset all flags to false.
    constexpr auto Clear() noexcept -> SceneFlags&
    {
        data_ = 0;
        return *this;
    }

    //! Clear all dirty flags without processing.
    constexpr auto ClearDirtyFlags() noexcept -> SceneFlags&
    {
        for (std::size_t i = 0; i < flag_count; ++i) {
            auto flag = static_cast<FlagEnum>(i);
            auto flag_state = GetFlag(flag);
            flag_state.SetDirtyBit(false);
            SetFlag(flag, flag_state);
        }
        return *this;
    }

    //! Set inheritance state for all flags.
    constexpr auto SetInheritedAll(bool state) noexcept -> SceneFlags&
    {
        for (std::size_t i = 0; i < flag_count; ++i) {
            auto flag = static_cast<FlagEnum>(i);
            auto flag_state = GetFlag(flag);
            flag_state.SetInherited(state);
            SetFlag(flag, flag_state);
        }
        return *this;
    }

    //! Update all inherited flags from parent.
    constexpr auto UpdateAllInheritFromParent(const SceneFlags& parent) noexcept -> SceneFlags&
    {
        for (std::size_t i = 0; i < flag_count; ++i) {
            auto flag = static_cast<FlagEnum>(i);
            if (auto flag_state = GetFlag(flag); flag_state.IsInherited()) {
                flag_state.UpdateValueFromParent(parent.GetEffectiveValue(flag));
                SetFlag(flag, flag_state);
            }
        }
        return *this;
    }

    //! Count flags that are currently dirty.
    [[nodiscard]] constexpr auto CountDirtyFlags() const noexcept -> std::size_t
    {
        std::size_t count = 0;
        for (std::size_t i = 0; i < flag_count; ++i) {
            if (auto flag = static_cast<FlagEnum>(i); IsDirty(flag)) {
                ++count;
            }
        }
        return count;
    }

    //! Process all dirty flags.
    constexpr auto ProcessDirtyFlags() noexcept -> bool
    {
        bool status { true };
        for (std::size_t i = 0; i < flag_count; ++i) {
            auto flag = static_cast<FlagEnum>(i);
            if (auto flag_state = GetFlag(flag); flag_state.IsDirty()) {
                if (flag_state.ProcessDirty()) {
                    SetFlag(flag, flag_state);
                } else {
                    status = false; // At least one dirty flag was not processed
                }
            }
        }
        return status;
    }

    //=== Iteration Support ===-----------------------------------------------//

    //! Get an immutable iterator to the beginning.
    [[nodiscard]] constexpr auto begin() const noexcept -> const_iterator
    {
        return const_iterator { *this, 0 };
    }

    //! Get an immutable iterator to the end.
    [[nodiscard]] constexpr auto end() const noexcept -> const_iterator
    {
        return const_iterator { *this, flag_count };
    }

    //=== Range Views ===-----------------------------------------------------//

    //! Get a view of all dirty flags.
    [[nodiscard]] constexpr auto dirty_flags() const noexcept
    {
        return std::views::iota(std::size_t { 0 }, flag_count)
            | std::views::transform([](std::size_t i) {
                  return static_cast<FlagEnum>(i);
              })
            | std::views::filter([this](FlagEnum flag) {
                  return this->IsDirty(flag);
              });
    }

    //! Get a view of all flags that inherit from parent.
    [[nodiscard]] constexpr auto inherited_flags() const noexcept
    {
        return std::views::iota(std::size_t { 0 }, flag_count)
            | std::views::transform([](std::size_t i) {
                  return static_cast<FlagEnum>(i);
              })
            | std::views::filter([this](FlagEnum flag) {
                  return this->IsInherited(flag);
              });
    }

    //! Get a view of all flags with effective value = true.
    [[nodiscard]] constexpr auto effective_true_flags() const noexcept
    {
        return std::views::iota(std::size_t { 0 }, flag_count)
            | std::views::transform([](std::size_t i) {
                  return static_cast<FlagEnum>(i);
              })
            | std::views::filter(
                [this](FlagEnum flag) {
                    return this->GetEffectiveValue(flag);
                });
    }

    //! Get a view of all flags with effective value = false.
    [[nodiscard]] constexpr auto effective_false_flags() const noexcept
    {
        return std::views::iota(std::size_t { 0 }, flag_count)
            | std::views::transform([](std::size_t i) {
                  return static_cast<FlagEnum>(i);
              })
            | std::views::filter(
                [this](FlagEnum flag) {
                    return !this->GetEffectiveValue(flag);
                });
    }

    //=== Comparison Operations ===------------------------------------------//

    //! Equality comparison.
    [[nodiscard]] constexpr auto
    operator==(const SceneFlags& other) const noexcept
    {
        return data_ == other.data_;
    }

    //! Inequality comparison.
    [[nodiscard]] constexpr auto operator!=(const SceneFlags& other) const noexcept
    {
        return !(*this == other);
    }

    //=== Raw Data Access ===------------------------------------------------//

    //! Get raw storage value.
    [[nodiscard]] constexpr auto Raw() const noexcept -> storage_type
    {
        return data_;
    }

    //! Set raw storage value.
    constexpr auto SetRaw(const storage_type value) noexcept -> SceneFlags&
    {
        data_ = value;
        return *this;
    }

private:
    storage_type data_ = 0;

    //! Get all bits for a flag.
    [[nodiscard]] constexpr auto GetFlagBits(const std::size_t index) const noexcept -> std::uint8_t
    {
        const auto shift = index * bits_per_flag;
        return static_cast<std::uint8_t>((data_ >> shift) & flag_mask);
    }

    //! Set all bits for a flag.
    constexpr auto SetFlagBits(const std::size_t index, std::uint8_t bits) noexcept -> void
    {
        const auto shift = index * bits_per_flag;
        const auto mask = storage_type { flag_mask } << shift;
        data_ = (data_ & ~mask) | ((storage_type { bits } & flag_mask) << shift);
    }

    //! Get the full flag state.
    /*!
     This is a flag returned by value, any modifications to it will not affect
     the original SceneFlags. It is used for the const_iterator and for setting
     all flags in bulk.
    */
    [[nodiscard]] constexpr auto GetFlag(FlagEnum flag) const noexcept -> SceneFlag
    {
        return SceneFlag(GetFlagBits(static_cast<std::size_t>(flag)));
    }
};

//=== Iterator Implementation ===---------------------------------------------//

//! Const iterator for SceneFlags.
/*!
 Provides a forward iterator over the SceneFlags container, allowing iteration
 through all flags and their states. The iterator returns pairs of flag enum and
 SceneFlag state. The flag state is returned by value, not allowing direct
 modification of the original SceneFlags. To apply modifications, use SetFlag().

 \tparam FlagEnum Enum type that must satisfy SceneFlagEnum concept.
*/
template <SceneFlagEnum FlagEnum>
class SceneFlags<FlagEnum>::const_iterator {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = std::pair<FlagEnum, SceneFlag>;
    using difference_type = std::ptrdiff_t;
    using pointer = const value_type*;
    using reference = value_type; // returns by value

    //! Default constructor.
    constexpr const_iterator() noexcept
        : flags_(nullptr)
        , index_(0)
    {
    }

    //! Constructor for iterator at specific position.
    constexpr const_iterator(const SceneFlags& flags, const std::size_t index) noexcept
        : flags_(&flags)
        , index_(index)
    {
    }

    //! Dereference operator returns flag-state pair.
    [[nodiscard]] constexpr auto operator*() const noexcept -> value_type
    {
        return { static_cast<FlagEnum>(index_), flags_->GetFlag(static_cast<FlagEnum>(index_)) };
    }

    //! Arrow proxy for member access.
    struct ArrowProxy {
        value_type value;
        constexpr auto operator->() const noexcept -> const value_type* { return &value; }
    };

    //! Member access operator.
    [[nodiscard]] constexpr auto operator->() const noexcept -> ArrowProxy
    {
        return { **this };
    }

    //! Pre-increment operator.
    constexpr auto operator++() noexcept -> const_iterator&
    {
        ++index_;
        return *this;
    }

    //! Post-increment operator.
    constexpr auto operator++(int) noexcept -> const_iterator
    {
        auto tmp = *this;
        ++index_;
        return tmp;
    }

    //! Equality comparison.
    [[nodiscard]] constexpr auto operator==(const const_iterator& other) const noexcept -> bool
    {
        if (flags_ == nullptr) {
            return other.flags_ == nullptr;
        }
        if (other.flags_ == nullptr) {
            return false;
        }
        return flags_ == other.flags_ && index_ == other.index_;
    }

    //! Inequality comparison.
    [[nodiscard]] constexpr auto operator!=(const const_iterator& other) const noexcept -> bool
    {
        return !(*this == other);
    }

private:
    const SceneFlags* flags_;
    std::size_t index_;
};

//=== Range Adapters (Global) ===---------------------------------------------//

//! Global range adapter for dirty flags.
template <SceneFlagEnum FlagEnum>
[[nodiscard]] constexpr auto dirty_flags(const SceneFlags<FlagEnum>& flags)
{
    return flags | std::views::filter([](const auto& pair) {
        return pair.second.IsDirty();
    });
}

//! Global range adapter for inherited flags.
template <SceneFlagEnum FlagEnum>
[[nodiscard]] constexpr auto inherited_flags(const SceneFlags<FlagEnum>& flags)
{
    return flags | std::views::filter([](const auto& pair) {
        return pair.second.IsInherited();
    });
}

//! Global range adapter for flags with effective value = true.
template <SceneFlagEnum FlagEnum>
[[nodiscard]] constexpr auto effective_true_flags(const SceneFlags<FlagEnum>& flags)
{
    return flags | std::views::filter([](const auto& pair) {
        return pair.second.GetEffectiveValue();
    });
}

//! Global range adapter for flags with effective value = false.
template <SceneFlagEnum FlagEnum>
[[nodiscard]] constexpr auto effective_false_flags(const SceneFlags<FlagEnum>& flags)
{
    return flags | std::views::filter([](const auto& pair) {
        return !pair.second.GetEffectiveValue();
    });
}

//=== Atomic Specialization ===-----------------------------------------------//

//! Thread-safe atomic wrapper for SceneFlags with lock-free operations.
/*!
 Provides atomic access to SceneFlags for multi-threaded scene graph operations.
 All flag state modifications are performed atomically on the underlying 64-bit
 storage, ensuring thread safety without explicit locking.

 Supports standard atomic operations including load, store, exchange, and
 compare-exchange with configurable memory ordering guarantees. Particularly
 useful for shared scene state that needs to be accessed from multiple threads
 such as rendering and update threads.

 \tparam FlagEnum Enum type that must satisfy SceneFlagEnum concept
*/
template <SceneFlagEnum FlagEnum>
class AtomicSceneFlags {
public:
    using flags_type = SceneFlags<FlagEnum>;
    using storage_type = typename flags_type::storage_type;

    //! Default constructor initializes all flags to zero state.
    constexpr AtomicSceneFlags() noexcept = default;

    //! Constructor from existing SceneFlags instance.
    explicit AtomicSceneFlags(const flags_type& flags) noexcept
        : data_(flags.Raw())
    {
    }

    //! Atomically load the current flags state.
    /*!
     Returns a snapshot of the current flags state that can be safely
     read and modified without affecting the atomic storage.
    */
    [[nodiscard]] auto Load(std::memory_order order = std::memory_order_seq_cst) const noexcept -> flags_type
    {
        flags_type result;
        result.SetRaw(data_.load(order));
        return result;
    }

    //! Atomically store new flags state.
    /*!
     Replaces the entire flags state atomically. All previous flag
     states are overwritten with the new values.
    */
    auto Store(const flags_type& flags, std::memory_order order = std::memory_order_seq_cst) noexcept -> void
    {
        data_.store(flags.Raw(), order);
    }

    //! Atomically exchange flags state and return previous value.
    /*!
     Atomically replaces the current flags with new values and returns
     the previous state. Useful for atomic updates that need to know
     the previous value.
    */
    [[nodiscard]] auto Exchange(const flags_type& flags, std::memory_order order = std::memory_order_seq_cst) noexcept -> flags_type
    {
        flags_type result;
        result.SetRaw(data_.exchange(flags.Raw(), order));
        return result;
    }

    //! Atomically compare and exchange flags state (weak version).
    /*!
     Attempts to atomically replace the expected value with the desired value.
     May fail spuriously on some architectures, requiring retry logic.
     Generally preferred in loops due to better performance characteristics.

     Updates expected parameter with current value if exchange fails.
    */
    [[nodiscard]] auto CompareExchangeWeak(flags_type& expected, const flags_type& desired,
        std::memory_order order = std::memory_order_seq_cst) noexcept -> bool
    {
        auto expected_raw = expected.Raw();
        const auto result = data_.compare_exchange_weak(expected_raw, desired.Raw(), order);
        if (!result) {
            expected.SetRaw(expected_raw);
        }
        return result;
    }

    //! Atomically compare and exchange flags state (strong version).
    /*!
     Attempts to atomically replace the expected value with the desired value.
     Will not fail spuriously but may be slower than weak version.
     Preferred for single-shot operations outside of loops.

     Updates expected parameter with current value if exchange fails.
    */
    [[nodiscard]] auto CompareExchangeStrong(flags_type& expected, const flags_type& desired,
        std::memory_order order = std::memory_order_seq_cst) noexcept -> bool
    {
        auto expected_raw = expected.Raw();
        const auto result = data_.compare_exchange_strong(expected_raw, desired.Raw(), order);
        if (!result) {
            expected.SetRaw(expected_raw);
        }
        return result;
    }

private:
    std::atomic<storage_type> data_ { 0 };
};

} // namespace oxygen::scene
