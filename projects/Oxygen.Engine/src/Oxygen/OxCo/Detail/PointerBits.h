//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include "Oxygen/Base/Logging.h"

namespace oxygen::co::detail {

template <class T, class BitsT, int Width, bool Merged>
class PointerBitsImpl;

template <class T, class BitsT, int Width>
class PointerBitsImpl<T, BitsT, Width, true> {
    static constexpr uintptr_t kBitsMask = (1 << Width) - 1;

public:
    PointerBitsImpl() noexcept = default;
    PointerBitsImpl(T* ptr, BitsT bits) noexcept { Set(ptr, bits); }

    [[nodiscard]] auto Ptr() const noexcept -> T*
    {
        // NOLINTNEXTLINE(performance-no-int-to-ptr, *-pro-type-reinterpret-cast)
        return reinterpret_cast<T*>(data_ & ~kBitsMask);
    }

    [[nodiscard]] auto Bits() const noexcept -> BitsT
    {
        return static_cast<BitsT>(data_ & kBitsMask);
    }

    void Set(T* ptr, BitsT bits) noexcept
    {
        // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
        const auto p = reinterpret_cast<uintptr_t>(ptr);
        const auto b = static_cast<uintptr_t>(bits);
        CHECK_F(!(p & kBitsMask));
        CHECK_F(!(b & ~kBitsMask));
        data_ = (p | b);
    }

private:
    uintptr_t data_ = 0;
};

template <class T, class BitsT, int Width>
class PointerBitsImpl<T, BitsT, Width, false> {
public:
    PointerBitsImpl() noexcept = default;
    PointerBitsImpl(T* ptr, BitsT bits) noexcept { Set(ptr, bits); }

    [[nodiscard]] auto Ptr() const noexcept -> T* { return ptr_; }
    [[nodiscard]] auto Bits() const noexcept -> BitsT { return bits_; }
    void Set(T* ptr, BitsT bits) noexcept
    {
        ptr_ = ptr;
        bits_ = bits;
    }

private:
    T* ptr_ = nullptr;
    BitsT bits_ {};
};

//! A utility class which can store a pointer and a small integer (hopefully) in
//! unused lower bits of the pointer.If pointer does not have enough unused
//! bits, will degrade to a plain struct.
/*!
 \tparam T Type of the pointer.
 \tparam BitsT Type of the integer.
 \tparam Width Number of bits to use for the integer.
 \tparam Align The alignment in bytes of the type `T`.
*/
template <class T, class BitsT, int Width, int Align = alignof(T)>
using PointerBits = PointerBitsImpl<T, BitsT, Width, (Align >= (1 << Width))>;

} // namespace oxygen::co::detail
