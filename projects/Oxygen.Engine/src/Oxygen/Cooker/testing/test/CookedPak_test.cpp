//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Cooker/testing/CookedPak.h>
#include <Oxygen/Cooker/testing/GeometryBuilder.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/PakWorkspace.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::cooker::testing::CookedPak;
using oxygen::cooker::testing::GeometryBuilder;
using oxygen::cooker::testing::HasPakError;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::PakDiagnosticSummary;
using oxygen::cooker::testing::PakWorkspace;
using oxygen::cooker::testing::SeedBytes;
using oxygen::cooker::testing::SourceKeyOf;
using oxygen::serio::testing::BytesEqual;
namespace data = oxygen::data;

//! A pak of one root lists its assets in its directory, its browse index and
//! its catalog, under the root's mount.
NOLINT_TEST(CookedPakTest, ListsItsAssetsUnderItsMount)
{
  auto props = LooseCookedRoot { "Props" };
  const auto red = props.Material("Materials/Red.omat");
  const auto cube = props.Geometry("Geometry/Cube.ogeo");

  const auto pak = CookedPak { props }.Open();

  EXPECT_TRUE(pak.Pak().FindEntry(red).has_value());
  EXPECT_TRUE(pak.Pak().FindEntry(cube).has_value());
  EXPECT_EQ(
    pak.Pak().ResolveAssetKeyByVirtualPath("/Props/Materials/Red.omat"), red);
  EXPECT_EQ(pak.Pak().Catalog().mount, "Props");
  EXPECT_EQ(pak.Pak().Catalog().entries.size(), 2U);
}

//! A pak keeps the payload and the browse name of a texture of its root.
NOLINT_TEST(CookedPakTest, KeepsTexturePayloadsAndNames)
{
  auto game = LooseCookedRoot { "Game" };
  const auto payload = SeedBytes(5U, 64U);
  const auto grid = game.AddTexture(payload);
  const auto path = game.DescribeTexture("Textures/Grid.otex", grid);
  auto grid_material = oxygen::cooker::testing::MaterialBuilder { "Grid" };
  grid_material.Texture(
    &oxygen::data::pak::render::MaterialAssetDesc::base_color_texture, grid);
  static_cast<void>(game.Material("Materials/Grid.omat", grid_material));

  const auto pak = CookedPak { game }.Open();

  const auto row = pak.Pak().ResolveTextureIndexByVirtualPath(path);
  ASSERT_HAS_VALUE(row);
  EXPECT_THAT(
    pak.Payload<oxygen::data::TextureResource>(*row), BytesEqual(payload));
}

//! An asset that references an asset of a pak it depends on makes the pak
//! require it of that pak's mount.
NOLINT_TEST(CookedPakTest, DependencyReferencesBecomeRequirements)
{
  auto engine = LooseCookedRoot { "Engine" };
  const auto fallback = engine.Material("Materials/Default.omat");
  auto game = LooseCookedRoot { "Game" };
  auto crate = GeometryBuilder { "Crate" };
  crate.Submesh(crate.ProceduralLod(),
    data::SurfaceId::FromStableIdentity("crate"), fallback);
  static_cast<void>(game.Geometry("Props/Crate.ogeo", crate));

  const auto catalog
    = CookedPak { game }.DependsOn(CookedPak { engine }).Catalog();

  ASSERT_EQ(catalog.dependencies.size(), 1U);
  EXPECT_EQ(catalog.dependencies.front().mount, "Engine");
  EXPECT_THAT(catalog.dependencies.front().requirements,
    ::testing::Contains(data::PakRequirement {
      .kind = data::KeyReferenceKind::kAsset,
      .key = fallback,
      .type = data::AssetType::kMaterial,
    }));
}

//! A patch deletes the assets of its base its roots no longer hold.
NOLINT_TEST(CookedPakTest, PatchDeletesWhatItsRootsDropped)
{
  auto base = LooseCookedRoot { "Game" };
  static_cast<void>(base.Material("Materials/Kept.omat"));
  const auto dropped = base.Material("Materials/Dropped.omat");
  auto next = LooseCookedRoot { "Game" };
  static_cast<void>(next.Material("Materials/Kept.omat"));

  const auto catalog = CookedPak { next }.PatchOf(CookedPak { base }).Catalog();

  EXPECT_THAT(catalog.deleted, ::testing::ElementsAre(dropped));
  EXPECT_EQ(catalog.bases.size(), 1U);
  EXPECT_NE(catalog.source_key, CookedPak { base }.Catalog().source_key);
}

//! The builder packs the way the product does: the same root and identity
//! built by `CookedPak` and by `PakBuilder` give the same bytes.
NOLINT_TEST(CookedPakTest, MatchesThePakBuilder)
{
  auto game = LooseCookedRoot { "Game" };
  const auto wood = game.AddTexture(SeedBytes(4U, 32U));
  auto wood_material = oxygen::cooker::testing::MaterialBuilder { "Wood" };
  wood_material.Texture(
    &oxygen::data::pak::render::MaterialAssetDesc::base_color_texture, wood);
  static_cast<void>(game.Material("Materials/Wood.omat", wood_material));
  static_cast<void>(game.Scene("Scenes/Main.oscene"));
  const auto identity = SourceKeyOf(0x42U);
  auto workspace = PakWorkspace {};
  auto request = workspace.FullRequest({ workspace.Add(game) });
  request.source_key = identity;

  const auto built = PakWorkspace::Build(request);
  const auto bytes = CookedPak { game }.Identity(identity).Encode();

  EXPECT_FALSE(HasPakError(built.diagnostics))
    << PakDiagnosticSummary(built.diagnostics);
  EXPECT_THAT(
    bytes, BytesEqual(workspace.Storage().Bytes(request.output_pak_path)));
}

} // namespace
