//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <type_traits>
#include <utility>

#include <Oxygen/Base/Macros.h>

namespace oxygen::co::detail {

template <class T>
class IntrusivePtr;

//! A mixin for reference-counted objects.
template <class Self>
class RefCounted {
    size_t ref_count_ = 0;
    friend IntrusivePtr<Self>;

    OXYGEN_MAKE_NON_COPYABLE(RefCounted)
    OXYGEN_DEFAULT_MOVABLE(RefCounted)

    RefCounted() = default;
    ~RefCounted() = default;
    friend Self;
};

//! A lightweight replacement for `std::shared_ptr` which does not have the
//! overhead of atomic operations.
template <class T>
class IntrusivePtr { // NOLINT(cppcoreguidelines-special-member-functions)
    T* ptr_;

    void Ref()
    {
        if (ptr_) {
            ++ptr_->ref_count_;
        }
    }
    void DeRef()
    {
        if (ptr_ && --ptr_->ref_count_ == 0) {
            delete ptr_;
        }
    }

public:
    IntrusivePtr()
        : ptr_(nullptr)
    {
    }
    explicit(false) IntrusivePtr(std::nullptr_t)
        : ptr_(nullptr)
    {
    }
    explicit IntrusivePtr(T* ptr)
        : ptr_(ptr)
    {
        Ref();
    }
    IntrusivePtr(const IntrusivePtr& rhs)
        : ptr_(rhs.ptr_)
    {
        Ref();
    }

    auto operator=(IntrusivePtr rhs) -> IntrusivePtr&
    {
        // Implements both assignment operators, taking argument by value and
        // using std::swap.
        std::swap(ptr_, rhs.ptr_);
        return *this;
    }
    IntrusivePtr(IntrusivePtr&& rhs) noexcept
        : ptr_(std::exchange(rhs.ptr_, nullptr))
    {
    }
    ~IntrusivePtr()
    {
        // Delay the check until instantiation to allow incomplete types.
        static_assert(std::is_base_of_v<RefCounted<T>, T>,
            "T must be derived from RefCounted<T>");
        DeRef();
    }

    [[nodiscard]] auto Get() const noexcept -> T* { return ptr_; }
    [[nodiscard]] auto operator*() const noexcept -> T& { return *ptr_; }
    [[nodiscard]] auto operator->() const noexcept -> T* { return ptr_; }

    explicit operator bool() const noexcept { return ptr_ != nullptr; }
};

} // namespace oxygen::co::detail
