//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iostream>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Scene/Scene.h>

using oxygen::scene::Scene;
using oxygen::scene::SceneNode;

auto main(int /*argc*/, char** /*argv*/) -> int
{
  const auto scene = std::make_shared<Scene>("TestScene");
  std::cout << "Created scene: " << scene->GetName() << '\n';
  auto node = scene->CreateNode("TestNode");
  std::cout << "Created scene node: " << nostd::to_string(node) << '\n';
  return 0;
}
