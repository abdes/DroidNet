//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iostream>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>

using oxygen::data::MaterialAsset;
using oxygen::data::Mesh;

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

  auto material = oxygen::data::MaterialAsset::CreateDefault();

  // Use MeshBuilder to construct the mesh
  auto mesh = oxygen::data::MeshBuilder()
                .WithVertices(vertices)
                .WithIndices(indices)
                .BeginSubMesh("DefaultSubMesh", material)
                .WithMeshView({ .first_index = 0,
                  .index_count = 3,
                  .first_vertex = 0,
                  .vertex_count = 3 })
                .EndSubMesh()
                .Build();

  // TODO: exercise the renderer module

  return 0;
}
