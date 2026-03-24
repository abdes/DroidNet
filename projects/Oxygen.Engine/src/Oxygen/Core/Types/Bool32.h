//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <type_traits>

namespace oxygen {

//! ABI-stable boolean stored as a 32-bit scalar.
/*!
 Use this only when a contract explicitly requires 32-bit boolean storage, such
 as CPU <-> GPU structured-buffer ABI. This type intentionally does not accept
 arbitrary integers, so call sites preserve boolean semantics while the stored
 representation remains shader-safe.
 */
class Bool32 final {
public:
  constexpr Bool32() noexcept = default;
  constexpr Bool32(const bool value) noexcept
    : value_(value ? 1U : 0U)
  {
  }

  [[nodiscard]] constexpr auto RawValue() const noexcept -> std::uint32_t
  {
    return value_;
  }

  [[nodiscard]] constexpr auto IsTrue() const noexcept -> bool
  {
    return value_ != 0U;
  }

  [[nodiscard]] constexpr auto IsFalse() const noexcept -> bool
  {
    return value_ == 0U;
  }

  constexpr explicit operator bool() const noexcept { return IsTrue(); }

  constexpr auto operator==(const Bool32&) const noexcept -> bool = default;

private:
  std::uint32_t value_ { 0U };
};

static_assert(std::is_standard_layout_v<Bool32>);
static_assert(std::is_trivially_copyable_v<Bool32>);
static_assert(sizeof(Bool32) == sizeof(std::uint32_t));
static_assert(alignof(Bool32) == alignof(std::uint32_t));

} // namespace oxygen
