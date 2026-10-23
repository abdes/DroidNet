//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <limits>
#include <optional>
#include <type_traits>

namespace oxygen::scene::schema {

//! The valid values of a numeric scene property, as the scene property schema
//! declares them.
/*!
 Generated.SceneComponents.h holds one range per numeric field, in field units.
 An absent bound is unlimited, and a floating-point value must be finite
 whatever the bounds. Loading, cooking and the runtime reject a value outside
 its range; only an inspector edit may clamp.
*/
template <typename T>
  requires std::is_arithmetic_v<T>
struct PropertyRange {
  std::optional<T> min {};
  std::optional<T> max {};
  bool min_exclusive { false };
  bool max_exclusive { false };
  bool nonzero { false };

  //! Whether `value` is a valid value of the property.
  [[nodiscard]] constexpr auto Contains(const T value) const noexcept -> bool
  {
    if constexpr (std::is_floating_point_v<T>) {
      // A NaN fails both comparisons.
      if (!(value >= std::numeric_limits<T>::lowest()
            && value <= std::numeric_limits<T>::max())) {
        return false;
      }
    }
    if (nonzero && value == T {}) {
      return false;
    }
    if (min && (value < *min || (min_exclusive && value == *min))) {
      return false;
    }
    return !max || !(value > *max || (max_exclusive && value == *max));
  }
};

} // namespace oxygen::scene::schema
