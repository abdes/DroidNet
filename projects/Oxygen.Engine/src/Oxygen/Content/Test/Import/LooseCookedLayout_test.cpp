//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <gtest/gtest.h>

#include <Oxygen/Content/Import/LooseCookedLayout.h>
#include <Oxygen/Data/AssetType.h>

using oxygen::content::import::LooseCookedLayout;
using oxygen::data::AssetType;

namespace {

TEST(LooseCookedLayoutTest, DefaultSettings_AreCorrect)
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
  EXPECT_EQ(layout.descriptors_dir, "");
  EXPECT_EQ(layout.scenes_subdir, "Scenes");
  EXPECT_EQ(layout.geometry_subdir, "Geometry");
  EXPECT_EQ(layout.materials_subdir, "Materials");
}

TEST(LooseCookedLayoutTest, DescriptorFileNames_AreCorrect)
{
  EXPECT_EQ(
    LooseCookedLayout::MaterialDescriptorFileName("MyMat"), "MyMat.omat");
  EXPECT_EQ(
    LooseCookedLayout::GeometryDescriptorFileName("MyGeo"), "MyGeo.ogeo");
  EXPECT_EQ(
    LooseCookedLayout::SceneDescriptorFileName("MyScene"), "MyScene.oscene");
  EXPECT_EQ(LooseCookedLayout::PhysicsSceneDescriptorFileName("MyScene"),
    "MyScene.physics");
}

TEST(LooseCookedLayoutTest, DescriptorDirFor_YieldsExpectedDirs)
{
  LooseCookedLayout layout {};
  EXPECT_EQ(layout.DescriptorDirFor(AssetType::kMaterial), "Materials");
  EXPECT_EQ(layout.DescriptorDirFor(AssetType::kGeometry), "Geometry");
  EXPECT_EQ(layout.DescriptorDirFor(AssetType::kScene), "Scenes");
  EXPECT_EQ(layout.DescriptorDirFor(AssetType::kPhysicsScene), "Scenes");
  EXPECT_EQ(layout.DescriptorDirFor(AssetType::kUnknown), "");

  layout.descriptors_dir = "Assets";
  EXPECT_EQ(layout.DescriptorDirFor(AssetType::kMaterial), "Assets/Materials");
  EXPECT_EQ(layout.DescriptorDirFor(AssetType::kGeometry), "Assets/Geometry");
  EXPECT_EQ(layout.DescriptorDirFor(AssetType::kScene), "Assets/Scenes");

  // Setting empty subdirs
  layout.materials_subdir = "";
  EXPECT_EQ(layout.DescriptorDirFor(AssetType::kMaterial), "Assets");
}

TEST(LooseCookedLayoutTest, VirtualLeafPaths_AreCorrect)
{
  LooseCookedLayout layout {};
  EXPECT_EQ(layout.MaterialVirtualLeaf("M1"), "Materials/M1.omat");
  EXPECT_EQ(layout.GeometryVirtualLeaf("G1"), "Geometry/G1.ogeo");
  EXPECT_EQ(layout.SceneVirtualLeaf("S1"), "Scenes/S1.oscene");
  EXPECT_EQ(layout.PhysicsSceneVirtualLeaf("S1"), "Scenes/S1.physics");
}

TEST(LooseCookedLayoutTest, VirtualPaths_JoinCorrectly)
{
  LooseCookedLayout layout {};

  layout.virtual_mount_root = "/MountRoot";

  EXPECT_EQ(layout.MaterialVirtualPath("M1"), "/MountRoot/Materials/M1.omat");
  EXPECT_EQ(layout.GeometryVirtualPath("G1"), "/MountRoot/Geometry/G1.ogeo");

  layout.virtual_mount_root = "/";
  EXPECT_EQ(layout.SceneVirtualPath("S1"), "/Scenes/S1.oscene");

  layout.virtual_mount_root = "MyMount";
  EXPECT_EQ(layout.MaterialVirtualPath("M1"), "/MyMount/Materials/M1.omat");
}

TEST(LooseCookedLayoutTest, ResourcePaths_AreCorrect)
{
  LooseCookedLayout layout {};
  EXPECT_EQ(layout.BuffersTableRelPath(), "Resources/buffers.table");
  EXPECT_EQ(layout.BuffersDataRelPath(), "Resources/buffers.data");
  EXPECT_EQ(layout.TexturesTableRelPath(), "Resources/textures.table");
  EXPECT_EQ(layout.TexturesDataRelPath(), "Resources/textures.data");
  EXPECT_EQ(layout.PhysicsTableRelPath(), "Resources/physics.table");
  EXPECT_EQ(layout.PhysicsDataRelPath(), "Resources/physics.data");

  layout.resources_dir = "";
  EXPECT_EQ(layout.BuffersTableRelPath(), "buffers.table");
  EXPECT_EQ(layout.TexturesDataRelPath(), "textures.data");
}

} // namespace
