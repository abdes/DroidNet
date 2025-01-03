//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdlib>
#include <variant>

#include "Oxygen/Base/Types.h"

namespace oxygen::input {

class ActionValue
{
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

  void Update(bool update)
  {
    std::visit(
      Overload {
        [&update](bool& value) { value = update; },
        [&update](Axis1D& value) { value.x = (update) ? 1.0F : 0.0F; },
        [&update](Axis2D& value) { value.x = (update) ? 1.0F : 0.0F; },
      },
      value_);
  }

  void Update(const Axis1D& update)
  {
    std::visit(
      Overload {
        [&update](bool& value) { value = std::abs(update.x) > 0.0F; },
        [&update](Axis1D& value) { value.x = update.x; },
        [&update](Axis2D& value) { value.x = update.x; },
      },
      value_);
  }

  void Update(const Axis2D& update)
  {
    std::visit(
      Overload {
        [&update](bool& value) { value = std::abs(update.x) > 0.0F; },
        [&update](Axis1D& value) { value.x = update.x; },
        [&update](Axis2D& value) {
          value.x = update.x;
          value.y = update.y;
        },
      },
      value_);
  }

  template <typename T>
  [[nodiscard]] auto GetAs() const -> const T&
  {
    return std::get<T>(value_);
  }

  [[nodiscard]] auto IsActuated(float threshold) const -> bool
  {
    bool actuated { false };
    std::visit(Overload {
                 [&actuated, &threshold](bool value) {
                   actuated = (static_cast<float>(value) > threshold);
                 },
                 [&actuated, &threshold](const Axis1D& value) {
                   actuated = (std::abs(value.x) > threshold);
                 },
                 [&actuated, &threshold](const Axis2D& value) {
                   actuated = (std::abs(value.x) > threshold
                     || std::abs(value.y) > threshold);
                 },
               },
      value_);
    return actuated;
  }

 private:
  using ValueType = std::variant<bool, Axis1D, Axis2D>;
  ValueType value_;
};

} // namespace oxygen::input
