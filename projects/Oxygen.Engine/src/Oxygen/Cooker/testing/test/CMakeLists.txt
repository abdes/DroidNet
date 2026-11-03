# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# The builders are tested before any test uses them, all in memory.
oxygen_cooker_test(
  Testing
  UNIT
  SOURCES
    "AssetDescriptors_test.cpp"
    "CookedBytes_test.cpp"
    "CookedPak_test.cpp"
    "DescriptorDefects_test.cpp"
    "LooseCookedRoot_test.cpp"
    "MemoryFileSystem_test.cpp"
    "ModelRecord_test.cpp"
    "PakWorkspace_test.cpp"
    "PhysicsSidecarBuilder_test.cpp"
    "SceneBuilder_test.cpp"
    "ScriptBuilder_test.cpp"
)
