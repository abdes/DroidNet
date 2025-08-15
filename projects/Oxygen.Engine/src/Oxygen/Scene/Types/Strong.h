//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

namespace oxygen::scene {

struct NormalizedDistance {
  float value;
  explicit constexpr NormalizedDistance(const float v = 0.0f)
    : value(v)
  {
  }
  constexpr auto operator<=>(const NormalizedDistance&) const = default;
  constexpr explicit operator float() const noexcept { return value; }
};

struct ScreenSpaceError {
  float value;
  explicit constexpr ScreenSpaceError(const float v = 0.0f)
    : value(v)
  {
  }
  constexpr auto operator<=>(const ScreenSpaceError&) const = default;
  constexpr explicit operator float() const noexcept { return value; }
};

} // namespace oxygen::scene
