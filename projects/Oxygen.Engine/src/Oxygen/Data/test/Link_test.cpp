//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Link test entry point for Data module.
/*!
 @file Link_test.cpp

 Ensures procedural mesh generation code links & executes by invoking
 GenerateMesh on a cube asset and printing a short summary. This is not a
 unit test (no assertions); it's used to surface unresolved symbol or ODR
 issues during the link stage for Data-related objects.

 ### Notes

 - Prints vertex/index counts and submesh/view breakdown.
 - Exits with failure if mesh generation returns null.
 - Kept minimal intentionally; expand only if link coverage gaps appear.

 @see GenerateMesh
*/

// Standard library
#include <iostream>

// Project
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>

//! Simple link test verifying that GenerateMesh compiles & runs; prints mesh
//! summary (not a unit test, used for linker validation of Data module).

using oxygen::data::GenerateMesh;

auto main(int /*argc*/, char** /*argv*/) -> int
{
  // Create a simple cube mesh using the procedural mesh factory
  const auto mesh = GenerateMesh("Cube/TestCube", {});
  if (!mesh) {
    std::cerr << "Failed to create mesh.\n";
    return EXIT_FAILURE;
  }

  std::cout << "Mesh created: " << mesh->Vertices().size() << " vertices, "
            << mesh->IndexBuffer().Count() << " indices, "
            << mesh->SubMeshes().size() << " submeshes\n";

  // Print views for each submesh
  for (const auto& submesh : mesh->SubMeshes()) {
    std::cout << "  SubMesh '" << submesh.GetName() << "' has "
              << submesh.MeshViews().size() << " view(s):\n";
    std::size_t view_idx = 0;
    for (const auto& view : submesh.MeshViews()) {
      std::cout << "    View " << view_idx++ << ": " << view.Vertices().size()
                << " vertices, " << view.IndexBuffer().Count() << " indices\n";
    }
  }

  return EXIT_SUCCESS;
}
