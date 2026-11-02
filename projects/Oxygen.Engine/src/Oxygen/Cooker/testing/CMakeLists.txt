# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# The cooked content builders: loose cooked roots, paks, pak requests and
# defects, built in memory from the product's serde. Linked by tests only:
# Content, Cooker, the tools and the examples. Its own tests are in test/,
# added from Cooker/CMakeLists.txt once oxygen_cooker_test() exists.

add_library(
  Oxygen.Cooker.Testing
  STATIC
  "AssetDescriptors.cpp"
  "AssetDescriptors.h"
  "CookedBytes.cpp"
  "CookedBytes.h"
  "CookedPak.cpp"
  "CookedPak.h"
  "DescriptorDefects.cpp"
  "DescriptorDefects.h"
  "GeometryBuilder.cpp"
  "GeometryBuilder.h"
  "Descriptors.h"
  "LooseCookedRoot.cpp"
  "LooseCookedRoot.h"
  "MaterialBuilder.cpp"
  "MaterialBuilder.h"
  "Materialize.cpp"
  "Materialize.h"
  "MemoryFileSystem.h"
  "ModelRecord.cpp"
  "ModelRecord.h"
  "PackedPak.cpp"
  "PackedPak.h"
  "PakWorkspace.cpp"
  "PakWorkspace.h"
  "PhysicsSidecarBuilder.cpp"
  "PhysicsSidecarBuilder.h"
  "ProductLoad.h"
  "ProductRefusal.cpp"
  "ProductRefusal.h"
  "SceneBuilder.cpp"
  "SceneBuilder.h"
  "ScriptBuilder.cpp"
  "ScriptBuilder.h"
  "Seeds.h"
)
add_library(oxygen::cooker::testing ALIAS Oxygen.Cooker.Testing)
set_target_properties(
  Oxygen.Cooker.Testing
  PROPERTIES
    FOLDER
      "Testing"
)
target_link_libraries(
  Oxygen.Cooker.Testing
  PUBLIC
    oxygen::cooker
    oxygen::content
    oxygen::data
    oxygen::serio
)
