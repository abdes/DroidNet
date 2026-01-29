//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/ActiveScene.h"
#include "DemoShell/Internal/SceneControlBlock.h"

namespace oxygen::examples {

ActiveScene::ActiveScene(observer_ptr<internal::SceneControlBlock> control)
  : control_(control)
  , generation_snapshot_(
      control ? control->generation_.load(std::memory_order_acquire) : 0U)
{
}

//! Access the active scene after validating the generation.
auto ActiveScene::operator->() const -> scene::Scene*
{
  (void)Validate(true);
  return control_->scene_ptr_.load(std::memory_order_acquire);
}

auto ActiveScene::Validate(const bool abort_on_failure) const noexcept -> bool
{
  if (!control_) {
    if (abort_on_failure) {
      CHECK_F(false, "ActiveScene invalid: no control block");
    }
    return false;
  }

  const uint64_t current_generation
    = control_->generation_.load(std::memory_order_acquire);
  if (current_generation != generation_snapshot_) {
    if (abort_on_failure) {
      CHECK_F(false,
        "ActiveScene stale: expected generation={}, current generation={}",
        generation_snapshot_, current_generation);
    }
    return false;
  }

  if (control_->scene_ptr_.load(std::memory_order_acquire) == nullptr) {
    if (abort_on_failure) {
      CHECK_F(false, "ActiveScene invalid: no active scene");
    }
    return false;
  }

  return true;
}

//! Convert ActiveScene to a diagnostic string using ADL (nostd::to_string).
inline auto to_string(const ActiveScene& scene) -> std::string
{
  const bool is_valid = scene.IsValid();
  return std::string("ActiveScene{valid=") + (is_valid ? "true" : "false")
    + "}";
}

} // namespace oxygen::examples
