//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Macros.h"

#include <memory>
#include <span>
#include <type_traits>
#include <utility>

namespace oxygen::co::detail {

//! Custom queue for storing all deferred coroutines in a contiguous memory
//! block with no minimal allocations.
/*!
 The queue uses a circular buffer to store the coroutines. The buffer is
 allocated with an initial size and the queue is able to store a maximum of
 `capacity` coroutines. Provided that the executor is drained regularly (every
 frame), the queue will not allocate any memory after the initial allocation.

 Should it requires more space, the queue will allocate a new buffer with twice
 the size of the previous one and move the queued coroutines to the new buffer.
 This guarantees that the coroutines are always stored in a contiguous memory
 block.
*/
template <typename T>
    requires(std::is_nothrow_destructible_v<T>)
class [[nodiscard]] Queue {
    T* buffer_;
    size_t capacity_;
    T* head_;
    T* tail_;
    size_t size_ { 0 };

public:
    explicit Queue(const size_t capacity)
        : buffer_(static_cast<T*>(malloc(capacity * sizeof(T)))) // NOLINT(*-no-malloc)
        , capacity_(capacity)
        , head_(buffer_)
        , tail_(buffer_)
    {
    }

    OXYGEN_MAKE_NON_COPYABLE(Queue)

    Queue(Queue&& rhs) noexcept
        : buffer_(std::exchange(rhs.buffer_, nullptr))
        , capacity_(std::exchange(rhs.capacity_, 0))
        , head_(std::exchange(rhs.head_, nullptr))
        , tail_(std::exchange(rhs.tail_, nullptr))
        , size_(std::exchange(rhs.size_, 0))
    {
    }

    auto operator=(Queue&& rhs) noexcept -> Queue&
    {
        if (this != &rhs) {
            std::swap(buffer_, rhs.buffer_);
            std::swap(capacity_, rhs.capacity_);
            std::swap(head_, rhs.head_);
            std::swap(tail_, rhs.tail_);
            std::swap(size_, rhs.size_);
        }
        return *this;
    }

    ~Queue() noexcept
    {
        auto destroy_range = [](std::span<T> range) {
            std::destroy(range.begin(), range.end());
        };
        destroy_range(FirstRange());
        destroy_range(SecondRange());
        free(buffer_);
    }

    [[nodiscard]] auto Capacity() const { return capacity_; }
    [[nodiscard]] auto Size() const { return size_; }
    [[nodiscard]] auto Empty() const { return size_ == 0; }

    auto Front() -> T& { return *head_; }
    void PopFront()
    {
        head_->~T();
        if (++head_ == buffer_ + capacity_) { // NOLINT(*-pro-bounds-pointer-arithmetic)
            head_ = buffer_;
        }
        --size_;
    }

    template <class U>
    void PushBack(U&& u)
    {
        if (size_ == capacity_) {
            Grow();
        }
        new (tail_) T(std::forward<U>(u));
        if (++tail_ == buffer_ + capacity_) { // NOLINT(*-pro-bounds-pointer-arithmetic)
            tail_ = buffer_;
        }
        ++size_;
    }

    template <class... Args>
    void EmplaceBack(Args&&... args)
    {
        if (size_ == capacity_) {
            Grow();
        }
        new (tail_) T(std::forward<Args>(args)...);
        if (++tail_ == buffer_ + capacity_) { // NOLINT(*-pro-bounds-pointer-arithmetic)
            tail_ = buffer_;
        }
        ++size_;
    }

    template <std::invocable<T&> Fn>
    void ForEach(Fn&& fn)
    {
        for (T& elem : FirstRange()) {
            std::forward<Fn>(fn)(elem);
        }
        for (T& elem : SecondRange()) {
            std::forward<Fn>(fn)(elem);
        }
    }

private:
    auto FirstRange() -> std::span<T>
    {
        if (Empty()) {
            return {};
        }
        if (head_ < tail_) {
            return { head_, tail_ };
        }
        return { head_, buffer_ + capacity_ };
    }

    auto SecondRange() -> std::span<T>
    {
        if (Empty() || head_ < tail_) {
            return {};
        }
        return { buffer_, tail_ };
    }

    void Grow()
    {
        Queue q(std::max<size_t>(8, capacity_ * 2));
        auto move_range = [&](std::span<T> range) {
            if constexpr (std::is_nothrow_move_constructible_v<T>) {
                q.tail_ = std::uninitialized_move(range.begin(), range.end(),
                    q.tail_);
            } else {
                q.tail_ = std::uninitialized_copy(range.begin(), range.end(),
                    q.tail_);
            }
            q.size_ += range.size();
        };
        move_range(FirstRange());
        move_range(SecondRange());
        *this = std::move(q);
    }
};

} // namespace oxygen::co::detail
