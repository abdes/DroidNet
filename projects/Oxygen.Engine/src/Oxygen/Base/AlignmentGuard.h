//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <stack>
#include <stdexcept>

#include <Oxygen/Base/Logging.h>

namespace oxygen::serio {

//! Base class for Writer to allow guard access
class Packer {
  friend class AlignmentGuard;

protected:
  static constexpr uint16_t kMaxAlignment = 256;

  void pack_push(uint16_t alignment)
  {
    // 0 means auto-align to the type's required alignment (always valid)
    if (alignment != 0
      && (!std::has_single_bit(alignment) || alignment > kMaxAlignment)) {
      throw std::invalid_argument("invalid alignment value");
    }
    DLOG_F(INFO, "Pushing alignment: {}", static_cast<int>(alignment));
    alignment_.push(alignment);
  }

  void pack_pop()
  {
    DLOG_F(INFO, "Popping alignment: {}", static_cast<int>(alignment_.top()));
    alignment_.pop();
  }

  std::stack<uint16_t> alignment_ {};
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
    obj_.get().pack_push(alignment);
  }

  AlignmentGuard(const AlignmentGuard&) = delete;
  AlignmentGuard& operator=(const AlignmentGuard&) = delete;

  AlignmentGuard(AlignmentGuard&& other) noexcept
    : obj_(other.obj_)
    , active_(other.active_)
  {
    other.active_ = false;
  }

  AlignmentGuard& operator=(AlignmentGuard&& other) noexcept
  {
    if (this != &other) {
      if (active_) {
        obj_.get().pack_pop();
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
      obj_.get().pack_pop();
    }
  }

private:
  std::reference_wrapper<Packer> obj_;
  bool active_;
};

} // namespace oxygen::serio
