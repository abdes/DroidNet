//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Loose/LooseCookedLayout.h

#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::content::import::LooseCookedLayout;

namespace {

NOLINT_TEST(LooseCookedLayoutTest, DefaultsToStandardFileAndDirectoryNames)
{
  LooseCookedLayout layout {};
  EXPECT_EQ(layout.index_file_name, "container.index.bin");
  EXPECT_EQ(layout.resources_dir, "Resources");
  EXPECT_EQ(layout.buffers_table_file_name, "buffers.table");
  EXPECT_EQ(layout.buffers_data_file_name, "buffers.data");
  EXPECT_EQ(layout.textures_table_file_name, "textures.table");
  EXPECT_EQ(layout.textures_data_file_name, "textures.data");
  EXPECT_EQ(layout.physics_table_file_name, "physics.table");
  EXPECT_EQ(layout.physics_data_file_name, "physics.data");
  EXPECT_EQ(layout.scripts_table_file_name, "scripts.table");
  EXPECT_EQ(layout.scripts_data_file_name, "scripts.data");
  EXPECT_EQ(layout.physics_dir, "Physics");
  EXPECT_EQ(layout.physics_resources_subdir, "Resources");
}

NOLINT_TEST(LooseCookedLayoutTest, DescriptorVirtualPathJoinsTheMountRoot)
{
  LooseCookedLayout layout {};

  layout.virtual_mount_root = "/MountRoot";
  EXPECT_EQ(layout.DescriptorVirtualPath("Ship.glb/Paint.omat"),
    "/MountRoot/Ship.glb/Paint.omat");

  layout.virtual_mount_root = "/";
  EXPECT_EQ(layout.DescriptorVirtualPath("Ship.glb/Ship.oscene"),
    "/Ship.glb/Ship.oscene");

  layout.virtual_mount_root = "MyMount";
  EXPECT_EQ(layout.DescriptorVirtualPath("Wood.omat"), "/MyMount/Wood.omat");
}

NOLINT_TEST(
  LooseCookedLayoutTest, ResolvesResourceTablesAndDataUnderResourcesDir)
{
  LooseCookedLayout layout {};
  EXPECT_EQ(layout.BuffersTableRelPath(), "Resources/buffers.table");
  EXPECT_EQ(layout.BuffersDataRelPath(), "Resources/buffers.data");
  EXPECT_EQ(layout.TexturesTableRelPath(), "Resources/textures.table");
  EXPECT_EQ(layout.TexturesDataRelPath(), "Resources/textures.data");
  EXPECT_EQ(layout.PhysicsTableRelPath(), "Physics/Resources/physics.table");
  EXPECT_EQ(layout.PhysicsDataRelPath(), "Physics/Resources/physics.data");
  EXPECT_EQ(layout.ScriptsTableRelPath(), "Resources/scripts.table");
  EXPECT_EQ(layout.ScriptsDataRelPath(), "Resources/scripts.data");

  layout.resources_dir = "";
  EXPECT_EQ(layout.BuffersTableRelPath(), "buffers.table");
  EXPECT_EQ(layout.TexturesDataRelPath(), "textures.data");
  EXPECT_EQ(layout.PhysicsTableRelPath(), "Physics/Resources/physics.table");
  EXPECT_EQ(layout.PhysicsDataRelPath(), "Physics/Resources/physics.data");
}

} // namespace
