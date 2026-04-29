//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <cstdint>

namespace oxygen::interop::module {

  //! Stable identifier for the component class an editor property targets.
  /*!
   This is the *coarse* dimension of the property pipeline §5.3 wire
   vocabulary. The fine dimension (component-local field id) is owned
   by each component's IComponentPropertyApplier and never appears in
   this enum.

   Property pipeline routing:
     managed PropertyId ─► PropertyEntry { component, field, value }
                          │
                          └─► PropertyApplierRegistry.Find(component)
                              └─► applier.Apply(node, entries…)

   Adding a new component class requires:
     1. one new entry here (assign next free integer; never reuse);
     2. one new IComponentPropertyApplier subclass with its own
        local field enum and a small static dispatch table;
     3. one registration call in PropertyApplierRegistry::Bootstrap.
  */
  enum class ComponentId : std::uint16_t {
    kInvalid = 0,

    //! oxygen::scene::TransformComponent (per-node local transform).
    kTransform = 1,

    // Future: lights, cameras, scene environment, ...
  };

  //! One scalar entry on the property-pipeline wire.
  /*!
   Property pipeline §5.3 transports a flat sequence of these entries
   per target node. SetPropertiesCommand groups them by `component`
   and hands each contiguous run to the matching applier; the applier
   alone knows how to translate `field` into a component setter.

   The payload is intentionally a single `float` for the initial
   slice — vectors/colors/quaternions decompose into per-axis floats
   on the managed side. When non-scalar payloads are added, this
   becomes a tagged value; callers and appliers extend together.
  */
  struct PropertyEntry {
    ComponentId component;
    std::uint16_t field;
    float value;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
