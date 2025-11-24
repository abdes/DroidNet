//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Composition/Composition.h>

namespace oxygen::examples::common {

//! Minimal example-local Composition to hold components for examples.
/*! This class exists only so examples can add components via the composition
    API (AddComponent is protected on Composition). It does not touch engine
    types by itself — components are allowed to own native engine objects.
*/
class ExampleComposition : public oxygen::Composition {
public:
  ExampleComposition() = default;
  ~ExampleComposition() override = default;

  // Expose a thin wrapper to call the protected Composition::AddComponent
  // for examples. This keeps AddComponent usage in examples confined to an
  // example-local composition object.
  template <typename T, typename... Args>
  auto AddExampleComponent(Args&&... args) -> T&
  {
    return AddComponent<T>(std::forward<Args>(args)...);
  }
};

} // namespace oxygen::examples::common
