//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

namespace oxygen {

template <typename T, template <typename> class crtp_type> struct Crtp {
  constexpr T& underlying() noexcept { return static_cast<T&>(*this); }
  constexpr T const& underlying() const noexcept
  {
    return static_cast<T const&>(*this);
  }
};

} // namespace oxygen
