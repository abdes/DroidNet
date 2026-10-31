//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/LooseCookedWriter.cpp (re-cook retirement)

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "LooseCookedWriterTestSupport.h"

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Cooked/Validate.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/testing/Descriptors.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/TextureResourceDescriptor.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::testing {

namespace fixtures = oxygen::cooker::testing;

namespace {

  using oxygen::content::cooked::CookedContent;
  using oxygen::content::import::LooseCookedProducer;
  using oxygen::content::import::LooseCookedRecook;
  using oxygen::content::import::LooseCookedWriter;
  using oxygen::cooker::testing::MemoryFileSystem;
  using oxygen::data::AssetType;
  using oxygen::data::loose_cooked::FileKind;

  constexpr auto kShip = std::string_view { "/Content/Ship.glb" };
  constexpr auto kDock = std::string_view { "/Content/Dock.glb" };

  //! A model's identity in the cooked root, under which its products are.
  struct Identity final {
    std::string_view virtual_path;
    std::string_view relpath;

    [[nodiscard]] auto Product(const std::string_view leaf) const -> std::string
    {
      return std::string(virtual_path) + "/" + std::string(leaf);
    }

    [[nodiscard]] auto File(const std::string_view leaf) const -> std::string
    {
      return std::string(relpath) + "/" + std::string(leaf);
    }
  };

  constexpr auto kShipModel = Identity {
    .virtual_path = "/.cooked/Ship.glb",
    .relpath = "Ship.glb",
  };
  constexpr auto kShipCopy = Identity {
    .virtual_path = "/.cooked/Copy/Ship.glb",
    .relpath = "Copy/Ship.glb",
  };
  constexpr auto kDockModel = Identity {
    .virtual_path = "/.cooked/Dock.glb",
    .relpath = "Dock.glb",
  };

  auto Recook(const std::string_view source, const Identity& identity)
    -> LooseCookedRecook
  {
    return {
      .source = std::string(source),
      .identity = std::string(identity.virtual_path),
      .identity_relpath = std::string(identity.relpath),
    };
  }

  //! A material a source cooks under an identity.
  struct Material final {
    uint8_t seed = 0;
    std::string_view name;
    std::string_view source;
  };

  auto WriteMaterial(LooseCookedWriter& writer, const Identity& identity,
    const Material& material) -> void
  {
    const auto bytes = fixtures::MaterialDescriptor(
      material.name, fixtures::MaterialVariant::kPlain)
                         .bytes;
    const auto leaf = std::string(material.name) + ".omat";
    writer.WriteAssetDescriptor(MakeFirstByteAssetKey(material.seed),
      AssetType::kMaterial, identity.Product(leaf), identity.File(leaf), bytes,
      {}, FromSource(std::string(material.source)));
  }

  //! Cooks the ship: two materials, a texture descriptor and a buffer
  //! sidecar. The dock's material shares the root.
  auto CookShipAndDock(
    MemoryFileSystem& storage, const std::filesystem::path& cooked_root) -> void
  {
    storage.AddFile(cooked_root / kShipModel.File("Hull.otex"),
      data::EncodeTextureResourceDescriptor({
        .index = data::pak::core::ResourceIndexT { 1U },
        .descriptor = {},
      }));
    storage.AddFile(
      cooked_root / kShipModel.File("Hull.obuf"), std::vector<std::byte>(8U));

    LooseCookedWriter writer(cooked_root, storage);
    // The texture descriptor names row 1, so the root holds a texture table
    // with that row, and its (empty) payloads.
    writer.WriteFile(FileKind::kTexturesTable, "Resources/textures.table",
      std::vector<std::byte>(
        2U * sizeof(data::pak::core::TextureResourceDesc)));
    writer.WriteFile(FileKind::kTexturesData, "Resources/textures.data",
      std::vector<std::byte> {});
    WriteMaterial(
      writer, kShipModel, { .seed = 0x11, .name = "Hull", .source = kShip });
    WriteMaterial(
      writer, kShipModel, { .seed = 0x12, .name = "Sail", .source = kShip });
    WriteMaterial(
      writer, kDockModel, { .seed = 0x13, .name = "Pier", .source = kDock });
    writer.RegisterExternalTextureDescriptor(kShipModel.File("Hull.otex"),
      kShipModel.Product("Hull.otex"), FromSource(std::string(kShip)));
    writer.RegisterExternalAuxiliaryFile(
      kShipModel.File("Hull.obuf"), FromSource(std::string(kShip)));
    static_cast<void>(writer.Finish());
  }

  auto OpenCooked(const MemoryFileSystem& storage,
    const std::filesystem::path& cooked_root) -> CookedContent
  {
    return CookedContent::Open(storage, cooked_root);
  }

  auto AssetPaths(const CookedContent& content) -> std::vector<std::string>
  {
    auto paths = std::vector<std::string> {};
    std::ranges::transform(content.Assets(), std::back_inserter(paths),
      [](const auto& asset) -> std::string { return asset.virtual_path; });
    std::ranges::sort(paths);
    return paths;
  }

  //! Whether `content` lists a texture descriptor at `virtual_path`.
  auto ListsTexture(
    const CookedContent& content, const std::string_view virtual_path) -> bool
  {
    return content.FindNamedTexture(virtual_path) != nullptr;
  }

  //! Whether `content` lists the sidecar `name`.
  auto ListsSidecar(const CookedContent& content, const std::string_view name)
    -> bool
  {
    return std::ranges::any_of(content.Sidecars(),
      [name](const auto& sidecar) -> bool { return sidecar.name == name; });
  }

  /*!
   Scenario: the ship is re-cooked and no longer produces its sail material,
   its texture descriptor or its buffer sidecar. The re-cook retires them, so
   the index lists only current outputs and their files are gone; the dock,
   another source, keeps its material.
  */
  NOLINT_TEST(LooseCookedWriterRetirementTest,
    RecookRetiresTheOutputsItsSourceNoLongerWrites)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "retire_recook";
    CookShipAndDock(storage, cooked_root);

    LooseCookedWriter writer(cooked_root, storage);
    WriteMaterial(
      writer, kShipModel, { .seed = 0x11, .name = "Hull", .source = kShip });
    writer.RetireUnwrittenOutputs(Recook(kShip, kShipModel));
    const auto result = writer.Finish();

    auto removed = result.removed_files;
    std::ranges::sort(removed);
    EXPECT_EQ(removed,
      (std::vector { kShipModel.File("Hull.obuf"), kShipModel.File("Hull.otex"),
        kShipModel.File("Sail.omat") }));
    for (const auto& relpath : removed) {
      EXPECT_FALSE(Exists(storage, cooked_root / relpath)) << relpath;
    }
    const auto content = OpenCooked(storage, cooked_root);
    EXPECT_EQ(AssetPaths(content),
      (std::vector {
        kDockModel.Product("Pier.omat"), kShipModel.Product("Hull.omat") }));
    EXPECT_FALSE(ListsTexture(content, kShipModel.Product("Hull.otex")));
    EXPECT_FALSE(ListsSidecar(content, kShipModel.File("Hull.obuf")));
    EXPECT_NO_THROW(cooked::Validate(content));
  }

  /*!
   Scenario: the dock is re-cooked. It retires nothing of the ship, and the
   ship's buffer sidecar keeps its producer in the published index.
  */
  NOLINT_TEST(
    LooseCookedWriterRetirementTest, RecookOfAnotherSourceRetiresNothingElse)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "retire_other";
    CookShipAndDock(storage, cooked_root);

    LooseCookedWriter writer(cooked_root, storage);
    WriteMaterial(
      writer, kDockModel, { .seed = 0x13, .name = "Pier", .source = kDock });
    writer.RetireUnwrittenOutputs(Recook(kDock, kDockModel));
    const auto result = writer.Finish();

    EXPECT_TRUE(result.removed_files.empty());
    const auto content = OpenCooked(storage, cooked_root);
    EXPECT_EQ(content.Assets().size(), 3U);
    const auto buffer = std::ranges::find(content.Sidecars(),
      kShipModel.File("Hull.obuf"), &cooked::CookedSidecar::name);
    ASSERT_NE(buffer, content.Sidecars().end());
    EXPECT_EQ(buffer->source_path, kShip);
  }

  /*!
   Scenario: the ship's source is imported under a second identity. A re-cook
   of one identity retires only what lies under it, so the other identity's
   products of the same source stay.
  */
  NOLINT_TEST(LooseCookedWriterRetirementTest,
    RecookKeepsTheOutputsOfAnotherIdentityOfItsSource)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "retire_identity";
    CookShipAndDock(storage, cooked_root);
    {
      LooseCookedWriter writer(cooked_root, storage);
      WriteMaterial(
        writer, kShipCopy, { .seed = 0x21, .name = "Hull", .source = kShip });
      WriteMaterial(
        writer, kShipCopy, { .seed = 0x22, .name = "Sail", .source = kShip });
      static_cast<void>(writer.Finish());
    }

    LooseCookedWriter writer(cooked_root, storage);
    WriteMaterial(
      writer, kShipCopy, { .seed = 0x21, .name = "Hull", .source = kShip });
    writer.RetireUnwrittenOutputs(Recook(kShip, kShipCopy));
    const auto result = writer.Finish();

    EXPECT_EQ(
      result.removed_files, std::vector { kShipCopy.File("Sail.omat") });
    const auto content = OpenCooked(storage, cooked_root);
    EXPECT_EQ(AssetPaths(content),
      (std::vector { kShipCopy.Product("Hull.omat"),
        kDockModel.Product("Pier.omat"), kShipModel.Product("Hull.omat"),
        kShipModel.Product("Sail.omat") }));
    EXPECT_TRUE(ListsTexture(content, kShipModel.Product("Hull.otex")));
    EXPECT_TRUE(ListsSidecar(content, kShipModel.File("Hull.obuf")));
  }

  /*!
   Scenario: a physics sidecar completes the ship's scene, so it keeps the
   scene's producer, but it comes from a job of its own. A re-cook of the ship
   writes the scene again and keeps the sidecar.
  */
  NOLINT_TEST(
    LooseCookedWriterRetirementTest, RecookKeepsTheSidecarOfItsSceneProducer)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "retire_sidecar";
    const auto scene_key = MakeFirstByteAssetKey(0x31);
    const auto scene = fixtures::MakeEmptySceneDescriptor();
    const auto write_scene = [&](LooseCookedWriter& writer) -> void {
      writer.WriteAssetDescriptor(scene_key, AssetType::kScene,
        kShipModel.Product("Ship.oscene"), kShipModel.File("Ship.oscene"),
        scene, {}, FromSource(std::string(kShip)));
    };
    {
      LooseCookedWriter writer(cooked_root, storage);
      write_scene(writer);
      static_cast<void>(writer.Finish());
    }
    {
      LooseCookedWriter writer(cooked_root, storage);
      writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x32),
        AssetType::kPhysicsScene, kShipModel.Product("Ship.opscene"),
        kShipModel.File("Ship.opscene"),
        fixtures::PhysicsSidecarDescriptor(scene_key, scene).bytes,
        data::AssetReferences::Create({},
          {
            data::KeyReference {
              .key = scene_key,
              .kind = data::KeyReferenceKind::kLogical,
              .expected_type = AssetType::kScene,
            },
          })
          .value(),
        LooseCookedProducer {
          .source = {},
          .completes = kShipModel.Product("Ship.oscene"),
        });
      static_cast<void>(writer.Finish());
    }

    LooseCookedWriter writer(cooked_root, storage);
    write_scene(writer);
    writer.RetireUnwrittenOutputs(Recook(kShip, kShipModel));
    const auto result = writer.Finish();

    EXPECT_TRUE(result.removed_files.empty());
    const auto content = OpenCooked(storage, cooked_root);
    EXPECT_EQ(AssetPaths(content),
      (std::vector { kShipModel.Product("Ship.opscene"),
        kShipModel.Product("Ship.oscene") }));
    for (const auto& asset : content.Assets()) {
      EXPECT_EQ(asset.source_path, kShip);
    }
  }

  //! A re-cook names its source by mount path.
  NOLINT_TEST(
    LooseCookedWriterRetirementTest, RetireUnwrittenOutputsRequiresAMountPath)
  {
    auto storage = MemoryFileSystem {};
    LooseCookedWriter writer(MemoryRoot() / "retire_invalid", storage);

    EXPECT_THROW(writer.RetireUnwrittenOutputs(Recook("Ship.glb", kShipModel)),
      std::runtime_error);
  }

} // namespace

} // namespace oxygen::content::testing
