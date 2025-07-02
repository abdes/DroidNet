//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iostream>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Renderer/RenderItem.h>

using oxygen::data::Mesh;
using oxygen::engine::RenderItem;

auto main(int /*argc*/, char** /*argv*/) -> int
{
  // Create a dummy MeshAsset (normally loaded from file or procedural)
  std::vector<oxygen::data::Vertex> vertices
    = { { { 0, 0, 0 }, { 0, 0, 1 }, { 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 },
          { 1, 1, 1, 1 } },
        { { 1, 0, 0 }, { 0, 0, 1 }, { 1, 0 }, { 1, 0, 0 }, { 0, 1, 0 },
          { 1, 1, 1, 1 } },
        { { 0, 1, 0 }, { 0, 0, 1 }, { 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 },
          { 1, 1, 1, 1 } } };
  std::vector<std::uint32_t> indices = { 0, 1, 2 };
  auto mesh = std::make_shared<Mesh>(
    "TestMesh", std::move(vertices), std::move(indices));

  // Create a RenderItem
  RenderItem item;
  item.mesh = mesh;
  item.world_transform = glm::mat4(1.0f);
  item.UpdatedTransformedProperties();

  std::cout << "Bounding sphere: (" << item.bounding_sphere.x << ", "
            << item.bounding_sphere.y << ", " << item.bounding_sphere.z << ", "
            << item.bounding_sphere.w << ")\n";
  std::cout << "Bounding box min: (" << item.bounding_box_min.x << ", "
            << item.bounding_box_min.y << ", " << item.bounding_box_min.z
            << ")\n";
  std::cout << "Bounding box max: (" << item.bounding_box_max.x << ", "
            << item.bounding_box_max.y << ", " << item.bounding_box_max.z
            << ")\n";
  return 0;
}
