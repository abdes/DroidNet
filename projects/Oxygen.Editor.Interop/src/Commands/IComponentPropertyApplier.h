//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <span>

#include <Commands/PropertyKeys.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::interop::module {

  //! Per-component dispatcher for property-pipeline §5.3 entries.
  /*!
   Each component class (TransformComponent, MaterialOverrideComponent,
   …) owns one `IComponentPropertyApplier`. The applier carries a tiny
   static dispatch table keyed by *component-local* field ids — that
   table is the only place where wire id → setter mapping lives.

   Adding a new property to an existing component is one row in that
   table; adding a new component class is one new applier subclass and
   one registration call. Neither operation touches
   SetPropertiesCommand.
  */
  class IComponentPropertyApplier {
  public:
    IComponentPropertyApplier() = default;
    IComponentPropertyApplier(const IComponentPropertyApplier&) = delete;
    auto operator=(const IComponentPropertyApplier&)
      -> IComponentPropertyApplier& = delete;
    IComponentPropertyApplier(IComponentPropertyApplier&&) = delete;
    auto operator=(IComponentPropertyApplier&&)
      -> IComponentPropertyApplier& = delete;
    virtual ~IComponentPropertyApplier() = default;

    //! The component this applier handles.
    [[nodiscard]] virtual auto GetComponentId() const noexcept
      -> ComponentId = 0;

    //! Apply a contiguous run of entries (all targeting GetComponentId)
    //! to the given node. Implementations must read once, mutate
    //! locally, write back once per affected sub-field.
    virtual void Apply(oxygen::scene::SceneNode& node,
      std::span<const PropertyEntry> entries) = 0;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
