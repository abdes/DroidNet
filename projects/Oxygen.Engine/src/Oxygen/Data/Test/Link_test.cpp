//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iostream>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>

using oxygen::data::MakeCubeMeshAsset;

auto main(int /*argc*/, char** /*argv*/) -> int
{
  // Create a simple cube mesh using the procedural mesh factory
  const auto mesh = MakeCubeMeshAsset();
  if (!mesh) {
    std::cerr << "Failed to create mesh.\n";
    return EXIT_FAILURE;
  }

  std::cout << "Mesh created: " << mesh->Vertices().size() << " vertices, "
            << mesh->Indices().size() << " indices, "
            << mesh->SubMeshes().size() << " submeshes\n";

  // Print views for each submesh
  for (const auto& submesh : mesh->SubMeshes()) {
    std::cout << "  SubMesh '" << submesh.Name() << "' has "
              << submesh.MeshViews().size() << " view(s):\n";
    std::size_t view_idx = 0;
    for (const auto& view : submesh.MeshViews()) {
      std::cout << "    View " << view_idx++ << ": " << view.Vertices().size()
                << " vertices, " << view.Indices().size() << " indices\n";
    }
  }

  return EXIT_SUCCESS;
}
