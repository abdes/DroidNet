//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Composition/Component.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Graphics/Common/Internal/CommandListPool.h>

namespace oxygen::graphics::internal {

// Concrete component that provides DeferredReclaimer semantics for the
// composition container. The public DeferredReclaimer header is intentionally
// free of component macros — this subclass remains internal and retains the
// component metadata and dependency on CommandListPool.
class DeferredReclaimerComponent final : public Component,
                                         public detail::DeferredReclaimer {
  OXYGEN_COMPONENT(DeferredReclaimerComponent)
  OXYGEN_COMPONENT_REQUIRES(oxygen::graphics::internal::CommandListPool)

public:
  DeferredReclaimerComponent() = default;
  OXGN_GFX_API ~DeferredReclaimerComponent() override = default;

  OXYGEN_MAKE_NON_COPYABLE(DeferredReclaimerComponent)
  OXYGEN_MAKE_NON_MOVABLE(DeferredReclaimerComponent)
};

} // namespace oxygen::graphics::internal
