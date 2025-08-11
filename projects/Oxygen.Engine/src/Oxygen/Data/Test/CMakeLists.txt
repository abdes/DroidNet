# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(GTest REQUIRED CONFIG)

# ==============================================================================
# Build instructions
# ==============================================================================

# -- Library Link Test ---------------------------------------------------------

add_executable("Oxygen.Data.LinkTest" "Link_test.cpp")
set_target_properties(
  "Oxygen.Data.LinkTest"
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(
  "Oxygen.Data.LinkTest"
  PRIVATE
    ${OXYGEN_COMMON_CXX_FLAGS}
)
target_link_libraries("Oxygen.Data.LinkTest" PRIVATE oxygen::data)
add_test(NAME "Oxygen.Data.LinkTest" COMMAND "Oxygen.Data.LinkTest")

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(
  All
  SOURCES
    "AssetKey_test.cpp"
    "BufferResource_test.cpp"
    "GeometryAsset_test.cpp"
    "IndexBufferView_test.cpp"
    "Mesh_test.cpp"
    "MeshBoundingSphere_test.cpp"
    "MeshBuilder_test.cpp"
    "MeshView_test.cpp"
    "ProceduralMeshes_test.cpp"
    "SubMesh_test.cpp"
    "TextureResource_test.cpp"
    "ToStringConverters_test.cpp"
    "Vertex_test.cpp"
)
