//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::examples {
class DemoShell;
}
namespace oxygen::examples::async {
enum class ShowcaseLighting : std::uint8_t { kDaylight, kSunset, kSpotlight };
//! One authored tour. Scene/camera changes last only until Explore or
//! destruction.
class AsyncShowcase final {
public:
  struct SceneBindings {
    observer_ptr<scene::Scene> scene;
    scene::SceneNode camera;
    scene::SceneNode sun;
    scene::SceneNode spotlight;
  };
  AsyncShowcase(DemoShell& shell, SceneBindings bindings);
  ~AsyncShowcase();
  OXYGEN_MAKE_NON_COPYABLE(AsyncShowcase)
  OXYGEN_MAKE_NON_MOVABLE(AsyncShowcase)
  auto Play() -> void;
  auto Pause() -> void;
  auto Explore() -> void;
  auto Preview(ShowcaseLighting mode) -> void;
  auto Update(double seconds) -> void;
  auto SetComparisonHeld(bool held) -> void;
  [[nodiscard]] auto ComparisonLabel() const -> const char*;
  [[nodiscard]] auto IsActive() const -> bool;
  [[nodiscard]] auto IsPaused() const -> bool;
  [[nodiscard]] auto IsPlaying() const -> bool;
  [[nodiscard]] auto Progress() const -> float;
  [[nodiscard]] auto Caption() const -> const char*;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace oxygen::examples::async
