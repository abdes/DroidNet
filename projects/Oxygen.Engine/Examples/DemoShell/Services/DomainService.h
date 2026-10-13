//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>

namespace oxygen {
namespace engine {
  class FrameContext;
}
namespace scene {
  class Scene;
  class SceneNode;
}
} // namespace oxygen

namespace oxygen::examples {

class DomainService {
public:
  DomainService() = default;
  virtual ~DomainService() = default;

  OXYGEN_MAKE_NON_COPYABLE(DomainService)
  OXYGEN_MAKE_NON_MOVABLE(DomainService)

  virtual void OnFrameStart(const engine::FrameContext& context) = 0;
  virtual void OnSceneActivated(scene::Scene& scene) = 0;
  //! The main view was published with `camera` over `viewport`.
  virtual void OnMainViewReady(ViewId view_id, const scene::SceneNode& camera,
    const ViewPort& viewport) = 0;

  [[nodiscard]] virtual auto GetEpoch() const noexcept -> std::uint64_t = 0;
};

} // namespace oxygen::examples
