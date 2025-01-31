//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/OxCo/Coroutine.h"

namespace oxygen::co::detail {

//! A common header of any coroutine frame, used by all major compilers
//! (gcc, clang, msvc-16.8+).
/*!
 \see https://devblogs.microsoft.com/oldnewthing/20211007-00/?p=105777.

 `await_suspend()` receives a `std::coroutine_handle` for indicating completion
 of the awaited operation, but in many cases we want to be able to intercept
 that completion without creating another full coroutine frame (due to the
 expense, heap-allocation, etc.). This is achieved by filling out a
 `CoroutineFrame` sub-object with function pointers of our choosing, and
 synthesizing a `std::coroutine_handle` that points to it.
*/
struct CoroutineFrame {
    //! `std::coroutine_handle<>::resume()` is effectively a call to this
    //! function pointer. For `Proxy Frames`, this is used as a callback.
    void (*resume_fn)(CoroutineFrame*) = nullptr;

    //! `std::coroutine_handle<>::destroy()` calls this. For `Proxy Frames`
    /// (where only `Resume()` is used), `destroy_fn` is re-purposed as a
    /// pointer to the coroutine frame for the parent coroutine.
    void (*destroy_fn)(CoroutineFrame*) = nullptr;

    //! Converts a `std::coroutine_handle` to a `CoroutineFrame*`.
    static auto FromHandle(const Handle h)
    {
        return static_cast<CoroutineFrame*>(h.address());
    }

    //! Converts a `CoroutineFrame` to a `std::coroutine_handle`.
    auto ToHandle() & { return Handle::from_address(this); }
};

namespace frame_tags {

    static_assert(alignof(CoroutineFrame) >= 4);

#if defined(__x86_64__) || defined(__aarch64__) || defined(_WIN64)
    // Allows distinguishing between frames constructed by the library (Proxy
    // Frames) vs. real coroutine frames constructed by the compiler.
    //
    // We are free to use two MSB of a pointer, which are unused on all modern
    // 64-bit architectures.
    //
    // https://en.wikipedia.org/wiki/Intel_5-level_paging
    constexpr uintptr_t kProxy = 1ULL << 63;

    // Tags a Proxy Frame used for Tasks.
    constexpr uintptr_t kTask = 1ULL << 62;

    constexpr bool kHaveSpareBitsInPointers = true;
#elif defined(__arm__) && !defined(__thumb__)
    // ARM processors have several instruction sets. The most-condensed
    // instruction set is called "Thumb" and has 16-bit long instructions:
    // https://stackoverflow.com/questions/10638130/what-is-the-arm-thumb-instruction-set
    //
    // These instructions are 2-byte aligned. Thus, it would seem like we have 1
    // LSB available to distinguish between PROXY frames and real coroutine
    // frames.
    //
    // However, we do NOT:
    // https://stackoverflow.com/questions/27084857/what-does-bx-lr-do-in-arm-assembly-language
    //
    // 1. A program may contain both 32-bit ARM and 16-bit Thumb instructions.
    // 2. A pointer to a function compiled with 16-bit Thumb instructions has 1
    //    bit in the LSB to trigger a CPU switch to Thumb state when calling the
    //    function.
    //
    // Thus, this trick with the LSB only work reliably when Thumb is disabled.
    constexpr uintptr_t kProxy = 1ul;

    // TASK is only used in Proxy Frames. A handle to a coroutine can be
    // linkTo()'d to a PROXY frame. Fortunately, a CoroutineFrame is at least
    // 4-byte aligned, which means we actually have 2 LSB available for tagging.
    constexpr uintptr_t kTask = 1ul << 1;

    constexpr bool HaveSpareBitsInPointers = true;
#else
    // Unknown architecture, or an architecture known not to have any spare bits
    // in a pointer to a function. We're going to fall back to magic numbers
    // in `destroy_fn` to tell between different frame types, and use an extra
    // pointer for an up-link.
    constexpr uintptr_t kProxy = 1;
    constexpr uintptr_t kTask = 2;
    constexpr bool kHaveSpareBitsInPointers = false;
#endif

    constexpr uintptr_t kMask = kProxy | kTask;

} // namespace frame_tags

} // namespace oxygen::co::detail
