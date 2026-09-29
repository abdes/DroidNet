//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <utility>

#include <Oxygen/Base/Macros.h>

namespace oxygen::content {

//! One residency request; destruction returns its exact cache usage.
class ResidencyPin final {
public:
  ResidencyPin() = default;
  //! Wrap the residency control supplied by an IAssetLoader implementation.
  explicit ResidencyPin(std::shared_ptr<void> control) noexcept
    : control_(std::move(control))
  {
  }
  ~ResidencyPin() = default;
  OXYGEN_MAKE_NON_COPYABLE(ResidencyPin)
  OXYGEN_DEFAULT_MOVABLE(ResidencyPin)

  [[nodiscard]] explicit operator bool() const noexcept
  {
    return static_cast<bool>(control_);
  }
  auto Reset() noexcept -> void { control_.reset(); }

private:
  std::shared_ptr<void> control_ {};
};

} // namespace oxygen::content
