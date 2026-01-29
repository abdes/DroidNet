//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::examples {

namespace internal {
  class SceneControlBlock;
} // namespace internal

//! Value object used by demos to access the active scene safely.
/*!
 ActiveScene validates the scene generation before access and aborts via
 CHECK_F when used after the active scene has changed or been cleared.
*/
class ActiveScene final {
public:
  ActiveScene() = default;
  explicit ActiveScene(observer_ptr<internal::SceneControlBlock> control);
  ~ActiveScene() = default;

  OXYGEN_DEFAULT_COPYABLE(ActiveScene)
  OXYGEN_DEFAULT_MOVABLE(ActiveScene)

  //! Returns true if the cached generation still matches the control block.
  [[nodiscard]] auto IsValid() const noexcept -> bool
  {
    return Validate(false);
  }

  //! Access the active scene after validating the generation.
  auto operator->() const -> scene::Scene*;

private:
  [[nodiscard]] auto Validate(const bool abort_on_failure) const noexcept
    -> bool;

  observer_ptr<internal::SceneControlBlock> control_ { nullptr };
  uint64_t generation_snapshot_ { 0U };
};

//! Convert ActiveScene to a diagnostic string using ADL (nostd::to_string).
[[nodiscard]] auto to_string(const ActiveScene& scene) -> std::string;

} // namespace oxygen::examples
