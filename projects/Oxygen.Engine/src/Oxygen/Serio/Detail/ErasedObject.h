//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include <Oxygen/Base/Macros.h>

namespace oxygen::serio::detail {

//! Owns one object of a type erased at construction, inline when it is small
//! and nothrow-movable, on the heap otherwise.
/*!
 The storage of every Serio erased wrapper. It knows how to move and destroy
 the object; the wrapper adds a table of the operations its concept needs. A
 moved-from storage holds no object.

 @tparam kInlineSize The largest object stored without a heap allocation.
*/
template <size_t kInlineSize> class ErasedObject {
public:
  //! Whether `T` is stored inline, so creating the storage cannot fail.
  template <class T>
  static constexpr bool kStoredInline
    = sizeof(T) <= kInlineSize && alignof(T) <= alignof(std::max_align_t)
    && std::is_nothrow_move_constructible_v<T>;

  //! Stores `value`. Throws `std::bad_alloc` when `T` is stored on the heap
  //! and the allocation fails.
  template <class T>
    requires(!std::is_same_v<T, ErasedObject>)
  explicit ErasedObject(T value) noexcept(kStoredInline<T>)
    : lifetime_(LifetimeFor<T>())
  {
    if constexpr (kStoredInline<T>) {
      // The erased storage owns the object; its Lifetime table destroys it.
      // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
      object_ = ::new (static_cast<void*>(buffer_.data())) T(std::move(value));
    } else {
      // The erased storage owns the object; its Lifetime table destroys it.
      // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
      object_ = std::make_unique<T>(std::move(value)).release();
    }
  }

  ~ErasedObject() noexcept { Reset(); }

  OXYGEN_MAKE_NON_COPYABLE(ErasedObject)

  ErasedObject(ErasedObject&& other) noexcept { TakeFrom(other); }

  auto operator=(ErasedObject&& other) noexcept -> ErasedObject&
  {
    if (this != &other) {
      Reset();
      TakeFrom(other);
    }
    return *this;
  }

  //! The stored object; null once moved from.
  [[nodiscard]] auto Get() noexcept -> void* { return object_; }

  //! The stored object; null once moved from.
  [[nodiscard]] auto Get() const noexcept -> const void* { return object_; }

private:
  struct Lifetime {
    void (*destroy)(ErasedObject& self) noexcept;
    void (*move)(ErasedObject& from, ErasedObject& to) noexcept;
  };

  template <class T> static auto LifetimeFor() noexcept -> const Lifetime*
  {
    static constexpr Lifetime kLifetime {
      .destroy = [](ErasedObject& self) noexcept -> void {
        auto* const object = static_cast<T*>(self.object_);
        if constexpr (kStoredInline<T>) {
          std::destroy_at(object);
        } else {
          std::default_delete<T> {}(object);
        }
      },
      // A relocation has a source and a destination of the same type.
      // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
      .move = [](ErasedObject& from, ErasedObject& to) noexcept -> void {
        if constexpr (kStoredInline<T>) {
          auto* const source = static_cast<T*>(from.object_);
          // The erased storage owns the object; its Lifetime destroys it.
          // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
          to.object_ = ::new (static_cast<void*>(to.buffer_.data()))
            T(std::move(*source));
          std::destroy_at(source);
        } else {
          to.object_ = from.object_;
        }
      },
    };
    return &kLifetime;
  }

  auto Reset() noexcept -> void
  {
    if (object_ != nullptr) {
      lifetime_->destroy(*this);
      object_ = nullptr;
    }
  }

  auto TakeFrom(ErasedObject& other) noexcept -> void
  {
    lifetime_ = other.lifetime_;
    if (other.object_ != nullptr) {
      lifetime_->move(other, *this);
      other.object_ = nullptr;
    }
  }

  alignas(std::max_align_t) std::array<std::byte, kInlineSize> buffer_ {};
  void* object_ { nullptr };
  const Lifetime* lifetime_ { nullptr };
};

} // namespace oxygen::serio::detail
