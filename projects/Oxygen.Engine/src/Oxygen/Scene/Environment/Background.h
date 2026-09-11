//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Environment/EnvironmentSystem.h>

namespace oxygen::scene::environment {

//! Display background composited behind the foreground after tone mapping.
/*!
 The color is linear SDR RGB in [0, 1]. Exposure and tone mapping affect the
 foreground independently. An enabled procedural atmosphere takes precedence.
 This background supplies no environment lighting or reflection radiance.
*/
class Background final : public EnvironmentSystem {
  OXYGEN_COMPONENT(Background)

public:
  //! Constructs a black background.
  Background() = default;

  //! Virtual destructor.
  ~Background() override = default;

  OXYGEN_DEFAULT_COPYABLE(Background)
  OXYGEN_DEFAULT_MOVABLE(Background)

  //! Sets the linear SDR background color.
  auto SetColorRgb(const Vec3& color) noexcept -> void { color_rgb_ = color; }

  //! Gets the linear SDR background color.
  [[nodiscard]] auto GetColorRgb() const noexcept -> const Vec3&
  {
    return color_rgb_;
  }

private:
  Vec3 color_rgb_ { 0.0F };
};

} // namespace oxygen::scene::environment
