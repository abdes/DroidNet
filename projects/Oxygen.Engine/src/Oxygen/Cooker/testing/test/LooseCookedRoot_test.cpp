//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/testing/CookedBytes.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::cooker::testing::CookedBytes;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::MemoryFileSystem;
using oxygen::cooker::testing::SeedBytes;
using oxygen::cooker::testing::SourceKeyOf;
using oxygen::serio::testing::BytesEqual;
namespace cooked = oxygen::content::cooked;
namespace base = oxygen::base;
namespace data = oxygen::data;

class LooseCookedRootTest : public ::testing::Test {
protected:
  //! Opens `root` through the product's read view.
  [[nodiscard]] auto Open(const LooseCookedRoot& root) -> cooked::CookedContent
  {
    const auto folder = std::filesystem::path("/cooked/root");
    storage_.AddFiles(folder, root.Files());
    return cooked::CookedContent::Open(storage_,
      data::CookedSource {
        .kind = data::CookedSourceKind::kLooseCooked,
        .path = folder,
      });
  }

private:
  MemoryFileSystem storage_;
};

//! The index the root writes loads with the product's index reader, with the
//! root's identity.
NOLINT_TEST_F(LooseCookedRootTest, IndexLoadsWithItsIdentity)
{
  auto game = LooseCookedRoot { "Game", 7U };
  static_cast<void>(game.Material("Materials/Wood.omat"));

  auto files = game.Files();
  const auto index = oxygen::content::lc::LooseCookedIndex::LoadFromBytes(
    CookedBytes::Index(files));

  EXPECT_EQ(index.Guid(), SourceKeyOf(7U));
}

//! Every asset method lists its asset at its virtual path, with its type and
//! the key of that path.
NOLINT_TEST_F(LooseCookedRootTest, AssetsAreListedAtTheirVirtualPaths)
{
  auto props = LooseCookedRoot { "Props" };
  const auto material = props.Material("Materials/Red.omat");
  const auto geometry = props.Geometry("Geometry/Cube.ogeo");
  const auto scene = props.Scene("Scenes/Main.oscene");

  const auto content = Open(props);

  ASSERT_EQ(content.Assets().size(), 3U);
  const auto* red = content.FindAsset("/Props/Materials/Red.omat");
  ASSERT_NE(red, nullptr);
  EXPECT_EQ(red->key, material);
  EXPECT_EQ(red->type, data::AssetType::kMaterial);
  ASSERT_NE(content.FindAsset(geometry), nullptr);
  EXPECT_EQ(content.FindAsset(geometry)->type, data::AssetType::kGeometry);
  ASSERT_NE(content.FindAsset(scene), nullptr);
  EXPECT_EQ(content.FindAsset(scene)->type, data::AssetType::kScene);
  EXPECT_EQ(content.Summary().mounts, std::vector<std::string> { "Props" });
}

//! Each asset method writes a descriptor of its type at the current version.
NOLINT_TEST_F(LooseCookedRootTest, DescriptorsCarryTheirTypeAndVersion)
{
  auto game = LooseCookedRoot { "Game" };
  const auto material = game.Material("Materials/Wood.omat");
  const auto geometry = game.Geometry("Geometry/Cube.ogeo");

  const auto content = Open(game);

  const auto header_of
    = [&content](const data::AssetKey& key) -> data::pak::core::AssetHeader {
    const auto bytes = content.Descriptor(key);
    auto header = data::pak::core::AssetHeader {};
    std::memcpy(&header, bytes.data(), sizeof(header));
    return header;
  };
  EXPECT_EQ(header_of(material).asset_type,
    static_cast<uint8_t>(data::AssetType::kMaterial));
  EXPECT_EQ(header_of(geometry).asset_type,
    static_cast<uint8_t>(data::AssetType::kGeometry));
}

//! The scene method's descriptor is one the scene parser accepts.
NOLINT_TEST_F(LooseCookedRootTest, SceneDescriptorParses)
{
  auto game = LooseCookedRoot { "Game" };
  const auto scene = game.Scene("Scenes/Main.oscene");

  const auto content = Open(game);

  EXPECT_NO_THROW(data::SceneAsset(scene, content.Descriptor(scene)));
}

//! A physics sidecar matches its scene as a cook makes it: the scene's key,
//! node count and content hash; a sidecar of a scene the root lacks is refused.
NOLINT_TEST_F(LooseCookedRootTest, PhysicsSidecarMatchesItsScene)
{
  auto game = LooseCookedRoot { "Game" };
  const auto scene = game.Scene("Scenes/Main.oscene");
  const auto sidecar
    = game.PhysicsSidecar("Scenes/Main.opscene", "Scenes/Main.oscene");

  const auto content = Open(game);

  const auto scene_bytes = content.Descriptor(scene);
  const auto sidecar_bytes = content.Descriptor(sidecar);
  auto descriptor = data::pak::physics::PhysicsSceneAssetDesc {};
  ASSERT_GE(sidecar_bytes.size(), sizeof(descriptor));
  std::memcpy(&descriptor, sidecar_bytes.data(), sizeof(descriptor));
  EXPECT_EQ(descriptor.target_scene_key, scene);
  EXPECT_EQ(descriptor.target_node_count,
    data::SceneAsset(scene, scene_bytes).GetNodes().size());
  const auto hash = base::ComputeSha256(scene_bytes);
  EXPECT_TRUE(std::ranges::equal(descriptor.target_scene_content_hash, hash));
  EXPECT_THROW(static_cast<void>(game.PhysicsSidecar("Other.opscene", "None")),
    std::invalid_argument);
}

//! A textured material references the texture row it names, and the row's
//! payload reads back.
NOLINT_TEST_F(LooseCookedRootTest, TexturedMaterialReferencesItsRow)
{
  auto game = LooseCookedRoot { "Game" };
  const auto payload = SeedBytes(9U, 16U);
  const auto wood = game.AddTexture(payload);
  const auto material = game.Material("Materials/Wood.omat", wood);

  const auto content = Open(game);

  const auto& textures = content.ResourceTable(cooked::TableKind::kTexture);
  EXPECT_EQ(textures.RowCount(), 2U);
  EXPECT_THAT(textures.Payload(wood), BytesEqual(payload));
  const auto* asset = content.FindAsset(material);
  ASSERT_NE(asset, nullptr);
  ASSERT_EQ(asset->references.Resources().size(), 1U);
  EXPECT_EQ(asset->references.Resources().front().index, wood);
}

//! A described texture is a named texture of its row.
NOLINT_TEST_F(LooseCookedRootTest, DescribedTextureIsNamed)
{
  auto game = LooseCookedRoot { "Game" };
  const auto grid = game.AddTexture(SeedBytes(3U));
  const auto path = game.DescribeTexture("Textures/Grid.otex", grid);

  const auto content = Open(game);

  const auto* named = content.FindNamedTexture(path);
  ASSERT_NE(named, nullptr);
  EXPECT_EQ(named->row, grid);
}

//! A root with no resource of a kind writes no table of it, unless kept.
NOLINT_TEST_F(LooseCookedRootTest, EmptyTablesAreLeftOutUnlessKept)
{
  auto game = LooseCookedRoot { "Game" };
  static_cast<void>(game.Material("Materials/Wood.omat"));
  game.AddEmptyTable(oxygen::cooker::testing::ResourceTable::kBuffers);

  const auto files = game.Files();

  EXPECT_TRUE(files.contains("buffers.table"));
  EXPECT_FALSE(files.contains("textures.table"));
}

} // namespace
