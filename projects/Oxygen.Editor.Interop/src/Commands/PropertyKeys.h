//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <cstdint>

#include <Oxygen/Scene/Schema/Generated.SceneComponents.h>

namespace oxygen::interop::module {

  //! Component and field ids of the engine's scene components schema.
  /*!
   The ids are generated from `Oxygen/Scene/Schema/SceneComponents.yaml`; a
   new component or field is a schema entry, never an edit here. `ComponentId`
   is the coarse dimension of the property-pipeline wire vocabulary; each
   `IComponentPropertyApplier` dispatches on its component's field enum.

   Property pipeline routing:
     managed PropertyId ─► PropertyEntry { component, field, value }
                          │
                          └─► PropertyApplierRegistry.Find(component)
                              └─► applier.Apply(node, entries…)
  */
  using scene::schema::ComponentId;
  using scene::schema::DirectionalLightField;
  using scene::schema::LocalFogVolumeField;
  using scene::schema::LocalLightField;
  using scene::schema::NodeField;
  using scene::schema::OrthographicCameraField;
  using scene::schema::PerspectiveCameraField;
  using scene::schema::TransformField;

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
