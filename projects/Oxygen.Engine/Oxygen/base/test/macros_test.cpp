//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "oxygen/base/macros.h"

#include <string>
#include <type_traits>

#include "gtest/gtest.h"

// NOLINTNEXTLINE
TEST(CommonMacros, NonCopyable) {
  // NOLINTNEXTLINE
  class NonCopyable
  {
  public:
    OXYGEN_MAKE_NON_COPYABLE(NonCopyable);
  };

  static_assert(!std::is_copy_constructible_v<NonCopyable>);
  static_assert(!std::is_assignable_v<NonCopyable, NonCopyable>);
}

// NOLINTNEXTLINE
TEST(CommonMacros, NonMoveable) {
  // NOLINTNEXTLINE
  class NonMoveable
  {
  public:
    OXYGEN_MAKE_NON_MOVEABLE(NonMoveable);
  };

  static_assert(!std::is_move_constructible_v<NonMoveable>);
  static_assert(!std::is_move_assignable_v<NonMoveable>);
}

// NOLINTNEXTLINE
TEST(CommonMacros, DefaultCopyable) {
  // NOLINTNEXTLINE
  class DefaultCopyable
  {
  public:
    OXYGEN_DEFAULT_COPYABLE(DefaultCopyable);
  };

  static_assert(std::is_copy_constructible_v<DefaultCopyable>);
  static_assert(std::is_assignable_v<DefaultCopyable, DefaultCopyable>);
}

// NOLINTNEXTLINE
TEST(CommonMacros, DefaultMoveable) {
  // NOLINTNEXTLINE
  class DefaultMoveable
  {
  public:
    DefaultMoveable() : member_("Hello World!") {}
    OXYGEN_DEFAULT_MOVABLE(DefaultMoveable);

  private:
    std::string member_;
  };

  const DefaultMoveable movable;
  (void)movable;

  static_assert(std::is_move_constructible_v<DefaultMoveable>);
  static_assert(std::is_move_assignable_v<DefaultMoveable>);
}
