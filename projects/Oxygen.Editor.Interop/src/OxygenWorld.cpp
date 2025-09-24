//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <OxygenWorld.h>

namespace Oxygen::Interop::World {

// Dummy placeholder implementation. Replace with real interop logic.
auto OxygenWorld::CreateScene(String ^ name) -> bool {
  // TODO: implement real scene creation bridge to Oxygen::Scene
  (void)name; // suppress unused parameter warning
  return false;
}

// Dummy placeholder implementation. Replace with real interop logic.
auto OxygenWorld::RemoveSceneNode(String ^ name) -> bool {
  // TODO: implement real scene node removal bridge to Oxygen::Scene
  (void)name; // suppress unused parameter warning
  return false;
}

} // namespace Oxygen::Interop::World
