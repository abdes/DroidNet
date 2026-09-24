//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Component.h>
#include <Oxygen/Graphics/Common/CommandRecording.h>
#include <Oxygen/Graphics/Common/Internal/DeferredReclaimerComponent.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics::internal {

//! Creates recording owners with the graphics device's fence retirement domain.
class Commander : public Component {
  OXYGEN_COMPONENT(Commander)
  OXYGEN_COMPONENT_REQUIRES(DeferredReclaimerComponent)

public:
  explicit Commander(detail::DeferredReclaimer& reclaimer)
    : reclaimer_(&reclaimer)
  {
  }
  ~Commander() override = default;
  OXYGEN_DEFAULT_COPYABLE(Commander)
  OXYGEN_DEFAULT_MOVABLE(Commander)

  //! Begins a recording whose caller owns submission or scope-exit policy.
  OXGN_GFX_NDAPI auto PrepareCommandRecorder(
    std::unique_ptr<CommandRecorder> recorder, SubmissionPolicy policy,
    std::shared_ptr<Graphics> backend_owner = {},
    std::shared_ptr<BackendLifetime> lifetime = {}) -> CommandRecording;

private:
  observer_ptr<detail::DeferredReclaimer> reclaimer_;
};

} // namespace oxygen::graphics::internal
