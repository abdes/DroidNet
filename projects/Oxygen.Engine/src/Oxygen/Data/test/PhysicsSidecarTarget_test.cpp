//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: the pairing of a physics sidecar with the scene it was cooked
// against: its key, its node count and its content hash.

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/testing/DescriptorDefects.h>
#include <Oxygen/Cooker/testing/PhysicsSidecarBuilder.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace data = oxygen::data;
using data::AssetKey;
using data::CookedDefect;
using oxygen::cooker::testing::DescriptorDefects;
using oxygen::cooker::testing::PhysicsSidecarBuilder;
using oxygen::cooker::testing::SceneBuilder;

auto Key(const std::string_view path) -> AssetKey
{
  return AssetKey::FromVirtualPath(path);
}

//! A scene of one root, and the sidecar of a rigid body on its root.
struct PairedScene final {
  SceneBuilder scene { "Main" };
  PhysicsSidecarBuilder bindings;
  std::vector<std::byte> scene_bytes;
  std::vector<std::byte> sidecar_bytes;

  PairedScene()
  {
    const auto root = scene.Node("Root");
    scene_bytes = scene.Bytes();
    bindings.RigidBody(
      root, Key("/Game/Crate.oshape"), Key("/Game/Wood.opmat"));
    sidecar_bytes
      = bindings.Bytes(Key("/Game/Main.oscene"), scene_bytes, "PhysicsScene");
  }
};

//! The scene `bytes` describe, under the key the sidecars target.
auto SceneOf(const std::span<const std::byte> bytes) -> data::SceneAsset
{
  return { Key("/Game/Main.oscene"), bytes };
}

//! The rule the sidecar `sidecar` breaks against `scene`; empty when it
//! targets that scene.
auto TargetDefect(const data::SceneAsset& scene,
  const std::span<const std::byte> sidecar) -> std::optional<CookedDefect>
{
  const auto sidecar_asset
    = data::PhysicsSceneAsset(Key("/Game/Main.opscene"), sidecar);
  try {
    sidecar_asset.CheckTarget(scene);
  } catch (const data::CookedFormatError& error) {
    return error.Defect();
  }
  return std::nullopt;
}

//! A sidecar targets the scene of its key, with its node count, as it was
//! when the sidecar was cooked.
NOLINT_TEST(PhysicsSidecarTargetTest, RefusesASceneOtherThanItsTarget)
{
  auto paired = PairedScene {};
  auto moved = paired.sidecar_bytes;
  DescriptorDefects::SetSidecarScene(moved, Key("/Game/Other.oscene"));
  auto counted = paired.sidecar_bytes;
  DescriptorDefects::SetSidecarNodeCount(counted, 2U);
  auto hashed = paired.sidecar_bytes;
  DescriptorDefects::CorruptSidecarSceneHash(hashed);
  auto recooked = SceneBuilder { "Main" };
  recooked.Node("Renamed");
  const auto recooked_bytes = recooked.Bytes();
  const auto scene = SceneOf(paired.scene_bytes);

  EXPECT_EQ(TargetDefect(scene, paired.sidecar_bytes), std::nullopt);
  EXPECT_EQ(TargetDefect(scene, moved), CookedDefect::kInvalidReference);
  EXPECT_EQ(TargetDefect(scene, counted), CookedDefect::kInconsistent);
  EXPECT_EQ(TargetDefect(scene, hashed), CookedDefect::kDigestMismatch);
  EXPECT_EQ(TargetDefect(SceneOf(recooked_bytes), paired.sidecar_bytes),
    CookedDefect::kDigestMismatch);
}

} // namespace
