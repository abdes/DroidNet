//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>

#include <iterator>
#include <stdexcept>
#include <utility>

namespace oxygen::co::detail {

template <typename T>
class IntrusiveList;

//! A CRTP mixin to add intrusive list item functionality to a class.
//! @tparam T The derived item class.
/*!
 The item stores pointers to the next and previous items in the list to make it
 fast to link and unlink items.
*/
template <typename T>
class IntrusiveListItem {
public:
    virtual ~IntrusiveListItem()
    {
        // Delay the assertion until the class is defined
        static_assert(std::is_base_of_v<IntrusiveListItem, T>);
        Unlink();
    }

protected:
    // Method to unlink the item from the list
    void Unlink()
    {
        if (prev_ && prev_->next_) {
            prev_->next_ = next_;
        }
        if (next_ && next_->prev_) {
            next_->prev_ = prev_;
        }
        next_ = prev_ = nullptr;
    }

private:
    // We don't allow direct construction of the list items in any form. Derived
    // classes can access the constructor, but only the list itself should be
    // movable. Moving will render the moved list empty, and the new list will
    // point to the original items.
    IntrusiveListItem() = default;
    OXYGEN_MAKE_NON_COPYABLE(IntrusiveListItem)
    OXYGEN_DEFAULT_MOVABLE(IntrusiveListItem)

    friend T; // Derived in the CRTP is friend to see the constructor
    friend class IntrusiveList<T>; // The list can be moved
    template <class, class>
    friend class IntrusiveListIterator; // The iterator needs to see the pointers

    IntrusiveListItem* next_ = nullptr;
    IntrusiveListItem* prev_ = nullptr;
};

template <typename T, typename Item>
class IntrusiveListIterator;

//! An intrusive linked list, which supports forward iteration over its items.
//! Items can be added at the front or at the back of the list, and any item can
//! easily unlink itself from the list.
template <typename T>
class IntrusiveList final : public IntrusiveListItem<T> {
public:
    using iterator = IntrusiveListIterator<T, IntrusiveListItem<T>>;
    using const_iterator = IntrusiveListIterator<const T, const IntrusiveListItem<T>>;

    IntrusiveList() { this->next_ = this->prev_ = this; }
    ~IntrusiveList() override { Clear(); }

    OXYGEN_DEFAULT_COPYABLE(IntrusiveList)

    IntrusiveList(IntrusiveList&& rhs) noexcept
    {
        if (rhs.Empty()) {
            this->next_ = this->prev_ = this;
        } else {
            IntrusiveListItem<T>* rhs_hook = &rhs;
            this->next_ = std::exchange(rhs.next_, rhs_hook);
            this->prev_ = std::exchange(rhs.prev_, rhs_hook);
            this->next_->prev_ = this->prev_->next_ = this;
        }
    }

    auto operator=(IntrusiveList&& rhs) noexcept -> IntrusiveList& = delete;

    void Clear()
    {
        while (!Empty()) {
            this->next_->Unlink();
        }
    }

    // Added begin() and end() member functions
    [[nodiscard]] auto begin() -> iterator { return iterator(static_cast<T*>(this->next_)); }
    [[nodiscard]] auto begin() const -> const_iterator { return const_iterator(static_cast<const T*>(this->next_)); }
    [[nodiscard]] auto end() -> iterator { return iterator(this); }
    [[nodiscard]] auto end() const -> const_iterator { return const_iterator(this); }

    [[nodiscard]] auto Empty() const { return this->next_ == this; }

    [[nodiscard]] auto ContainsOneItem() const
    {
        return !Empty() && this->next_->next_ == this;
    }

    void PushBack(T& item)
    {
        item.Unlink();
        item.next_ = this; // end sentinel
        item.prev_ = this->prev_; // the current back item
        this->prev_->next_ = &item; // update the next link of the list tail
        this->prev_ = &item; // make it the new back item
    }

    void PushFront(T& item)
    {
        item.Unlink();
        item.next_ = this->next_; // the current front item
        item.prev_ = this; // begin sentinel
        this->next_->prev_ = &item; // update the back-link of list head
        this->next_ = &item; // make it the new front items
    }

    void PopBack()
    {
        if (this->prev_ == this) {
            return;
        }
        this->prev_->Unlink();
    }

    void PopFront()
    {
        if (this->next_ == this) {
            return;
        }
        this->next_->Unlink();
    }

    // Front and back methods
    [[nodiscard]] auto Front() -> T&
    {
        if (this->next_ == this) {
            throw std::out_of_range("List is empty");
        }
        return *static_cast<T*>(this->next_);
    }

    [[nodiscard]] auto Front() const -> const T&
    {
        if (this->next_ == this) {
            throw std::out_of_range("List is empty");
        }
        return *static_cast<const T*>(this->next_);
    }

    [[nodiscard]] auto Back() -> T&
    {
        if (this->prev_ == this) {
            throw std::out_of_range("List is empty");
        }
        return *static_cast<T*>(this->prev_);
    }

    [[nodiscard]] auto Back() const -> const T&
    {
        if (this->prev_ == this) {
            throw std::out_of_range("List is empty");
        }
        return *static_cast<const T*>(this->prev_);
    }

    static void Erase(T& item) { item.Unlink(); }
};

template <class T, class Item>
class IntrusiveListIterator {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using reference = T&;

    IntrusiveListIterator() = default;

    explicit IntrusiveListIterator(Item* node)
        : node_(node)
    {
    }

    auto operator*() const -> reference { return *static_cast<T*>(node_); }
    auto operator->() const -> pointer { return static_cast<T*>(node_); }

    auto operator++() -> IntrusiveListIterator&
    {
        node_ = static_cast<pointer>(node_->next_);
        return *this;
    }

    auto operator++(int) -> IntrusiveListIterator
    {
        IntrusiveListIterator tmp = *this;
        ++*this;
        return tmp;
    }

    friend auto operator==(const IntrusiveListIterator& lhs, const IntrusiveListIterator& rhs) -> bool
    {
        return lhs.node_ == rhs.node_;
    }

    friend auto operator!=(const IntrusiveListIterator& lhs, const IntrusiveListIterator& rhs) -> bool
    {
        return lhs.node_ != rhs.node_;
    }

private:
    Item* node_ { nullptr };
};

} // namespace oxygen::co::detail
