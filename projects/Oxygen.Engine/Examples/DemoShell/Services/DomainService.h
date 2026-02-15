//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Base/Macros.h>

namespace oxygen {
namespace engine {
  class FrameContext;
}
namespace renderer {
  struct CompositionView;
}
namespace scene {
  class Scene;
}
} // namespace oxygen

namespace oxygen::examples {

//! Lifecycle interface for demo shell domain services.
/*!
 Defines the minimal lifecycle hooks used by demo shell services and exposes
 a change epoch for view model cache validation.

 @note Services must treat the provided contexts as immutable.
*/
class DomainService {
public:
  DomainService() = default;
  virtual ~DomainService() = default;

  OXYGEN_MAKE_NON_COPYABLE(DomainService)
  OXYGEN_MAKE_NON_MOVABLE(DomainService)

  //! Invoked at the start of the frame.
  virtual void OnFrameStart(const engine::FrameContext& context) = 0;

  //! Invoked after a new scene becomes active.
  virtual void OnSceneActivated(scene::Scene& scene) = 0;

  //! Invoked when the main view is ready in the frame context.
  virtual void OnMainViewReady(
    const engine::FrameContext& context, const renderer::CompositionView& view)
    = 0;

  //! Returns a monotonically increasing change epoch.
  [[nodiscard]] virtual auto GetEpoch() const noexcept -> std::uint64_t = 0;
};

} // namespace oxygen::examples
