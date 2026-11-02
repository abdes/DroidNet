//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <filesystem>
#include <stdexcept>

#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Cooker/testing/PhysicsSidecarBuilder.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::MemoryFileSystem;
using oxygen::cooker::testing::PhysicsSidecarBuilder;
using oxygen::cooker::testing::SceneBuilder;
namespace cooked = oxygen::content::cooked;
namespace data = oxygen::data;
namespace physics = oxygen::data::pak::physics;

auto Key(const char* path) -> data::AssetKey
{
  return data::AssetKey::FromVirtualPath(path);
}

//! Opens `root` through the product's read view.
auto Open(MemoryFileSystem& storage, const LooseCookedRoot& root)
  -> cooked::CookedContent
{
  const auto folder = std::filesystem::path("/cooked/root");
  storage.AddFiles(folder, root.Files());
  return cooked::CookedContent::Open(storage,
    data::CookedSource {
      .kind = data::CookedSourceKind::kLooseCooked,
      .path = folder,
    });
}

//! A sidecar's bindings read back by node, and it matches its scene as a cook
//! makes it: the scene's key, node count and content hash.
NOLINT_TEST(PhysicsSidecarBuilderTest, BindingsReadBackAndMatchTheScene)
{
  auto game = LooseCookedRoot { "Game" };
  auto main = SceneBuilder { "Main" };
  const auto root = main.Node("root");
  const auto crate = main.Node("crate", root);
  const auto door = main.Node("door", root);
  const auto scene = game.Scene("Scenes/Main.oscene", main);
  auto bindings = PhysicsSidecarBuilder {};
  bindings
    .RigidBody(crate, Key("/Game/Crate.oshape"), Key("/Game/Wood.opmat"),
      { .mass = 3.0F })
    .Collider(root, Key("/Game/Floor.oshape"), Key("/Game/Wood.opmat"))
    .Joint(door, crate, Key("/Game/Hinge.ophys"));
  const auto key = game.PhysicsSidecar("Scenes/Main.opscene", scene, bindings);

  auto storage = MemoryFileSystem {};
  const auto content = Open(storage, game);
  const auto sidecar = data::PhysicsSceneAsset(key, content.Descriptor(key));

  EXPECT_EQ(sidecar.GetTargetSceneKey(), scene);
  EXPECT_EQ(sidecar.GetTargetNodeCount(), 3U);
  EXPECT_NO_THROW(
    sidecar.CheckTarget(data::SceneAsset(scene, content.Descriptor(scene))));
  const auto bodies = sidecar.GetBindings<physics::RigidBodyBindingRecord>();
  ASSERT_EQ(bodies.size(), 1U);
  EXPECT_EQ(bodies[0].node_index, crate.Index());
  EXPECT_EQ(bodies[0].mass, 3.0F);
  EXPECT_EQ(sidecar.GetBindings<physics::ColliderBindingRecord>().size(), 1U);
  const auto joints = sidecar.GetBindings<physics::JointBindingRecord>();
  ASSERT_EQ(joints.size(), 1U);
  EXPECT_EQ(joints[0].node_index_b, crate.Index());
  const auto* asset = content.FindAsset(key);
  ASSERT_NE(asset, nullptr);
  EXPECT_TRUE(std::ranges::any_of(asset->references.Keys(),
    [&scene](const data::KeyReference& reference) -> bool {
      return reference.kind == data::KeyReferenceKind::kLogical
        && reference.key == scene;
    }));
}

//! Bindings of another scene's nodes, or of a node the scene lacks, are
//! refused where the sidecar is built.
NOLINT_TEST(PhysicsSidecarBuilderTest, RefusesNodesOfAnotherScene)
{
  auto game = LooseCookedRoot { "Game" };
  auto main = SceneBuilder { "Main" };
  static_cast<void>(main.Node("root"));
  auto other = SceneBuilder { "Other" };
  const auto root = other.Node("root");
  const auto deep = other.Node("deep", root);
  const auto scene = game.Scene("Scenes/Main.oscene", main);
  auto bindings = PhysicsSidecarBuilder {};
  bindings.Aggregate(deep);

  EXPECT_THROW(static_cast<void>(
                 game.PhysicsSidecar("Scenes/Main.opscene", scene, bindings)),
    std::invalid_argument);
  EXPECT_THROW(
    static_cast<void>(bindings.Bytes(scene, main.Bytes(), "PhysicsScene")),
    std::invalid_argument);
  EXPECT_THROW(bindings.Aggregate(main.Node("late")), std::invalid_argument);
}

} // namespace
