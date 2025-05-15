//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <utility>

#include <Oxygen/Base/Macros.h>

namespace oxygen::graphics {

//! Represents a range of indices for descriptors.
/*!
 Used to map between local descriptor indices (per descriptor type) and global
 indices. An empty range (count == 0) is valid and represents a no-op or
 sentinel value. This is useful for default construction, error states, or
 algorithms that operate on possibly-empty ranges.

 \note Bounds checking is the responsibility of the caller. IndexRange does not
       validate that its indices are within the bounds of any underlying
       resource or table. The user must ensure that the range is valid for the
       intended use.
*/
class IndexRange {
public:
    constexpr IndexRange() noexcept = default;

    constexpr IndexRange(uint32_t base_index, uint32_t count) noexcept
        : base_index_(base_index)
        , count_(count)
    {
    }

    ~IndexRange() noexcept = default;

    OXYGEN_DEFAULT_COPYABLE(IndexRange);
    OXYGEN_DEFAULT_MOVABLE(IndexRange);

    static constexpr auto Empty() noexcept { return IndexRange(); }

    [[nodiscard]] constexpr auto BaseIndex() const noexcept { return base_index_; }
    [[nodiscard]] constexpr auto EndIndex() const noexcept { return base_index_ + count_; }
    [[nodiscard]] constexpr auto Count() const noexcept { return count_; }
    [[nodiscard]] constexpr auto IsEmpty() const noexcept { return count_ == 0; }
    [[nodiscard]] constexpr auto Contains(uint32_t index) const noexcept
    {
        return index >= base_index_ && index < base_index_ + count_;
    }

    friend constexpr auto operator==(const IndexRange& a, const IndexRange& b) noexcept -> bool
    {
        return a.base_index_ == b.base_index_ && a.count_ == b.count_;
    }

    friend constexpr auto operator!=(const IndexRange& a, const IndexRange& b) noexcept -> bool
    {
        return !(a == b);
    }

    void swap(IndexRange& other) noexcept
    {
        using std::swap;
        swap(base_index_, other.base_index_);
        swap(count_, other.count_);
    }

private:
    uint32_t base_index_ = 0; //!< The base (starting) index of the range.
    uint32_t count_ = 0; //!< The number of indices in the range.
};

inline void swap(IndexRange& a, IndexRange& b) noexcept
{
    a.swap(b);
}

} // namespace oxygen::graphics
