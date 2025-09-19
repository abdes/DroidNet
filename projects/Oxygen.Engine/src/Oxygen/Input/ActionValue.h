//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cmath>
#include <type_traits>
#include <variant>

#include <Oxygen/Base/Types/Geometry.h>

namespace oxygen::input {

enum class ActionValueType : uint8_t {
  kBool = 0,
  kAxis1D = 1,
  kAxis2D = 2,
};

class ActionValue {
public:
  ActionValue() = default;

  explicit ActionValue(bool value)
    : value_(value)
  {
  }
  explicit ActionValue(Axis1D value)
    : value_(value)
  {
  }
  explicit ActionValue(Axis2D value)
    : value_(value)
  {
  }

  void Set(bool value) { value_ = value; }
  void Set(Axis1D value) { value_ = value; }
  void Set(Axis2D value) { value_ = value; }

  // Update with a boolean: set concrete representation accordingly.
  void Update(bool update)
  {
    std::visit(
      [&](auto& cur) noexcept {
        using T = std::remove_cvref_t<decltype(cur)>;
        if constexpr (std::is_same_v<T, bool>) {
          cur = update;
        } else if constexpr (std::is_same_v<T, Axis1D>) {
          cur.x = update ? 1.0F : 0.0F;
        } else if constexpr (std::is_same_v<T, Axis2D>) {
          cur.x = update ? 1.0F : 0.0F;
        }
      },
      value_);
  }

  // Update from Axis1D: translate into the concrete type stored in the variant.
  void Update(const Axis1D& update)
  {
    std::visit(
      [&](auto& cur) noexcept {
        using T = std::remove_cvref_t<decltype(cur)>;
        if constexpr (std::is_same_v<T, bool>) {
          cur = (std::abs(update.x) > 0.0F);
        } else if constexpr (std::is_same_v<T, Axis1D>) {
          cur.x = update.x;
        } else if constexpr (std::is_same_v<T, Axis2D>) {
          cur.x = update.x;
        }
      },
      value_);
  }

  // Update from Axis2D: translate into the concrete type stored in the variant.
  void Update(const Axis2D& update)
  {
    std::visit(
      [&](auto& cur) noexcept {
        using T = std::remove_cvref_t<decltype(cur)>;
        if constexpr (std::is_same_v<T, bool>) {
          cur = (std::abs(update.x) > 0.0F || std::abs(update.y) > 0.0F);
        } else if constexpr (std::is_same_v<T, Axis1D>) {
          cur.x = update.x;
        } else if constexpr (std::is_same_v<T, Axis2D>) {
          cur.x = update.x;
          cur.y = update.y;
        }
      },
      value_);
  }

  template <typename T> [[nodiscard]] auto GetAs() const -> const T&
  {
    return std::get<T>(value_);
  }
  [[nodiscard]] auto IsActuated(float threshold) const -> bool
  {
    return std::visit(
      [&](const auto& cur) noexcept {
        using T = std::remove_cvref_t<decltype(cur)>;
        if constexpr (std::is_same_v<T, bool>) {
          return static_cast<float>(cur) > threshold;
        } else if constexpr (std::is_same_v<T, Axis1D>) {
          return std::abs(cur.x) > threshold;
        } else /* Axis2D */ {
          return (std::abs(cur.x) > threshold) || (std::abs(cur.y) > threshold);
        }
      },
      value_);
  }

private:
  using ValueType = std::variant<bool, Axis1D, Axis2D>;
  // Default to false to keep a well-defined initial state.
  ValueType value_ { false };
};

} // namespace oxygen::input
