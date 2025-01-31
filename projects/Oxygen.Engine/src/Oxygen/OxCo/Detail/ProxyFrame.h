//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Logging.h"
#include "Oxygen/OxCo/Detail/CoRoutineFrame.h"

namespace oxygen::co::detail {

//! A CoroutineFrame constructed by the library, rather than the compiler.
/*!
 Allows intercepting the resumption of a parent task in order to do something
 other than immediately resuming the task that's backing it, such as propagate a
 cancellation instead.

 We also store a linkage pointer in the otherwise-unused `destroy_fn` field in
 order to allow re-construction of an async backtrace stack.
*/
template <bool HaveSpareBitsInPointers>
class ProxyFrameImpl;

template <>
class ProxyFrameImpl<true> : public CoroutineFrame {
public:
    static auto IsTagged(const uintptr_t tag, CoroutineFrame* f) -> bool
    {
        // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
        const auto fn_address = reinterpret_cast<uintptr_t>(f->destroy_fn);
        return (fn_address & tag) == tag;
    }

    // Make a link from `this` to `h` such that `h` will be returned from calls
    // to `FollowLink()`.
    void LinkTo(const Handle h)
    {
        // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
        const auto h_address = reinterpret_cast<uintptr_t>(h.address());
        DCHECK_EQ_F(h_address & frame_tags::kMask, 0ULL);
        // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
        const auto fn_address = reinterpret_cast<uintptr_t>(destroy_fn);
        // ReSharper disable CppClangTidyPerformanceNoIntToPtr
        // NOLINTNEXTLINE(*-pro-type-reinterpret-cast, *-no-int-to-ptr)
        destroy_fn = reinterpret_cast<decltype(destroy_fn)>(
            (h_address & ~frame_tags::kMask) | (fn_address & frame_tags::kMask));
    }

    // Returns a coroutine handle for the task that was linked to
    // `this` via `linkTo()`. Returns `nullptr` if no task has
    // been linked.
    [[nodiscard]] auto FollowLink() const -> Handle
    {
        // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
        const auto fn_address = reinterpret_cast<uintptr_t>(destroy_fn);
        const uintptr_t h_address = fn_address & ~frame_tags::kMask;
        // ReSharper disable CppClangTidyPerformanceNoIntToPtr
        // NOLINTNEXTLINE(*-pro-type-reinterpret-cast, *-no-int-to-ptr)
        return Handle::from_address(reinterpret_cast<void*>(h_address));
    }

protected:
    void TagWith(const uintptr_t tag)
    {
        // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
        auto fn_address = reinterpret_cast<uintptr_t>(destroy_fn);
        fn_address |= tag;
        // NOLINTNEXTLINE(*-pro-type-reinterpret-cast, *-no-int-to-ptr)
        destroy_fn = reinterpret_cast<decltype(destroy_fn)>(fn_address);
    }
};

// A version of the above for architectures not offering any spare bits
// in function pointers.
// Since coroutine frames are nevertheless at least 4-byte aligned,
// we still can reuse lower bits for tags.
template <>
class ProxyFrameImpl<false> : public CoroutineFrame {
public:
    void LinkTo(const Handle h)
    {
        // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
        const auto h_address = reinterpret_cast<uintptr_t>(h.address());
        DCHECK_EQ_F(h_address & frame_tags::kMask, 0ULL);
        // The Proxy Frame tag is stored in `CoroutineFrame::destroy_fn`.
        link_ = (link_ & frame_tags::kMask) | h_address;
    }

    [[nodiscard]] auto FollowLink() const -> Handle
    {
        const uintptr_t h_address = link_ & ~frame_tags::kMask;
        // NOLINTNEXTLINE(*-pro-type-reinterpret-cast, *-no-int-to-ptr)
        return Handle::from_address(reinterpret_cast<void*>(h_address));
    }

    static auto IsTagged(const uintptr_t tag, CoroutineFrame* f) -> bool
    {
        return f->destroy_fn == &ProxyFrameImpl::TagFn
            // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
            && (reinterpret_cast<ProxyFrameImpl*>(f)->link_ & tag) == tag;
    }

protected:
    void TagWith(const uintptr_t tag)
    {
        // We use `CoroutineFrame::destroy_fn` for storing links between coroutine
        // frames. Thus, we disallow `Awaitable`s to call `destroy()`.
        destroy_fn = &ProxyFrameImpl::TagFn;
        link_ = tag;
    }

private:
    static void TagFn(CoroutineFrame* /*unused*/) { }

    uintptr_t link_ = 0;
};

class ProxyFrame : public ProxyFrameImpl<frame_tags::kHaveSpareBitsInPointers> {
public:
    static constexpr uintptr_t kTag = frame_tags::kProxy;
    ProxyFrame() { TagWith(kTag); }
};

// Attempts a conversion from `CoroutineFrame` to `F`. Returns `nullptr` if `f`
// does not point to an `F`.
template <std::derived_from<CoroutineFrame> F>
[[nodiscard]] auto FrameCast(CoroutineFrame* f) -> F*
{
    return (f && ProxyFrame::IsTagged(F::kTag, f))
        ? static_cast<F*>(f)
        : nullptr;
}

} // namespace oxygen::co::detail
