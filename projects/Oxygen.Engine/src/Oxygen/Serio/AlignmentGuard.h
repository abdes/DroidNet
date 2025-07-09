//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <bit>
#include <cstdint>
#include <stack>
#include <stdexcept>

namespace oxygen::serio {

//! Base class for Writer to allow guard access
class Packer {
  friend class AlignmentGuard;

protected:
  using AlignmentT = uint16_t;
  static constexpr uint16_t kMaxAlignment = 256;

  auto PushAlignment(const AlignmentT alignment) -> void
  {
    // 0 means auto-align to the type's required alignment (always valid)
    if (alignment != 0
      && (!std::has_single_bit(alignment) || alignment > kMaxAlignment)) {
      throw std::invalid_argument("invalid alignment value");
    }
    alignment_.push(alignment);
  }

  auto PopAlignment() -> void { alignment_.pop(); }

  [[nodiscard]] auto CurrentAlignment() const noexcept -> AlignmentT
  {
    return alignment_.empty() ? 0 : alignment_.top();
  }

private:
  std::stack<AlignmentT> alignment_ {};
};

//! RAII helper for managing alignment stack via pack_push/pack_pop
/*!
  Pushes an alignment value on construction and pops it on destruction.

  ### Usage Examples

  ```cpp
  {
    AlignmentGuard guard(writer, 8);
    // ... code that requires 8-byte alignment ...
  } // alignment is automatically popped here
  ```

  @tparam T Any type satisfying PackAlignmentStack
  @param obj Reference to the object supporting pack_push/pack_pop
  @param alignment Alignment value to push

  @throw std::invalid_argument if alignment is invalid

  @see PackAlignmentStack
 */
class AlignmentGuard {
public:
  AlignmentGuard(Packer& obj, uint16_t alignment) noexcept(false)
    : obj_(obj)
    , active_(true)
  {
    obj_.get().PushAlignment(alignment);
  }

  AlignmentGuard(const AlignmentGuard&) = delete;
  auto operator=(const AlignmentGuard&) -> AlignmentGuard& = delete;

  AlignmentGuard(AlignmentGuard&& other) noexcept
    : obj_(other.obj_)
    , active_(other.active_)
  {
    other.active_ = false;
  }

  auto operator=(AlignmentGuard&& other) noexcept -> AlignmentGuard&
  {
    if (this != &other) {
      if (active_) {
        obj_.get().PopAlignment();
      }
      obj_ = other.obj_;
      active_ = other.active_;
      other.active_ = false;
    }
    return *this;
  }

  ~AlignmentGuard()
  {
    if (active_) {
      obj_.get().PopAlignment();
    }
  }

private:
  std::reference_wrapper<Packer> obj_;
  bool active_;
};

} // namespace oxygen::serio
