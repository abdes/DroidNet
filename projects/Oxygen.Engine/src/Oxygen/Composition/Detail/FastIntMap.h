//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Base/Macros.h>

namespace oxygen::composition::detail {

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-owning-memory)

//! High-performance hash map optimized for uint64_t key-value pairs
//!
//! Implements a hash table specifically optimized for storing and retrieving
//! 64-bit integer pairs. Uses quadratic probing for collision resolution and
//! maintains power-of-2 capacity for efficient indexing using bit operations.
class FastIntMap {
    //! Entry structure for hash table slots
    struct Entry {
        uint64_t key; //!< Hash key
        uint64_t value; //!< Associated value
        bool occupied; //!< Whether this slot contains valid data
    };

    Entry* entries_; //!< Dynamic array of hash table entries
    size_t capacity_; //!< Current table capacity (always power of 2)
    static constexpr float kLoadThreshold = 0.7F; //!< Load factor threshold for growth
    size_t size_ { 0 }; //!< Number of occupied slots

public:
    //! Creates a new hash map with specified initial capacity
    //! \param initial_capacity Desired initial capacity (rounded up to power of 2)
    explicit FastIntMap(const size_t initial_capacity = 64)
        : capacity_(RoundUpPow2(initial_capacity))
    {
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        entries_ = new Entry[capacity_](); // Zero-initialize
    }

    //! Destructor that frees allocated memory
    ~FastIntMap()
    {
        delete[] entries_;
    }

    OXYGEN_MAKE_NON_COPYABLE(FastIntMap)
    OXYGEN_MAKE_NON_MOVABLE(FastIntMap)

    //! Inserts or updates a key-value pair
    //! \param key The key to insert or update
    //! \param value The value to associate with the key
    void Insert(const uint64_t key, const uint64_t value)
    {
        // If adding one more element exceeds the load threshold, grow first
        if (static_cast<float>(size_ + 1) / static_cast<float>(capacity_) >= kLoadThreshold) {
            Grow();
        }

        const size_t mask = capacity_ - 1;
        size_t index = key & mask; // Faster than modulo

        size_t probe_count = 0;
        while (entries_[index].occupied && entries_[index].key != key) {
            probe_count++;
            if (probe_count >= capacity_) { // ← NEW check to avoid infinite loop
                Grow();
                Insert(key, value);
                return;
            }
            index = (index + probe_count) & mask;
        }

        // If it's a new slot, increment the size
        if (!entries_[index].occupied) {
            size_++;
        }

        entries_[index].key = key;
        entries_[index].value = value;
        entries_[index].occupied = true;
    }

    //! Retrieves the value associated with a key
    //! \param key The key to look up
    //! \param out_value Reference where the found value will be stored
    //! \return true if the key was found, false if not present
    auto Get(const uint64_t key, uint64_t& out_value) const -> bool
    {
        const size_t mask = capacity_ - 1;
        size_t index = key & mask;

        size_t probe_count = 0;
        while (entries_[index].occupied) {
            if (entries_[index].key == key) {
                out_value = entries_[index].value;
                return true;
            }
            probe_count++;
            index = (index + probe_count) & mask;

            if (probe_count >= capacity_) {
                return false;
            }
        }
        return false;
    }

private:
    //! Rounds up a number to the next power of 2
    //! \param v The value to round up
    //! \return Next power of 2 >= v
    static auto RoundUpPow2(size_t v) -> size_t
    {
        // NOLINTBEGIN(*-magic-numbers)
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v |= v >> 32;
        return v + 1;
        // NOLINTEND(*-magic-numbers)
    }

    //! Doubles the capacity of the hash table
    void Grow()
    {
        const Entry* old_entries = entries_;
        const size_t old_capacity = capacity_;

        auto cleanup = oxygen::Finally([&] {
            delete[] old_entries;
        });

        capacity_ *= 2;
        entries_ = new Entry[capacity_]();
        size_ = 0;

        for (size_t i = 0; i < old_capacity; i++) {
            if (old_entries[i].occupied) {
                Insert(old_entries[i].key, old_entries[i].value);
            }
        }
    }
};
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-owning-memory)

} // namespace oxygen::composition::detail
