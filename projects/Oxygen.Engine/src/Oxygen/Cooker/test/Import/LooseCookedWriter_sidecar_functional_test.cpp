//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/LooseCookedWriter.cpp (scene and physics sidecar
// regeneration unit)

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "LooseCookedWriterTestSupport.h"

#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Cooker/testing/PhysicsSidecarBuilder.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::testing {

namespace fixtures = oxygen::cooker::testing;

namespace {

  using oxygen::content::cooked::CookedContent;
  using oxygen::content::import::LooseCookedStaleSidecar;
  using oxygen::content::import::LooseCookedWriter;
  using oxygen::cooker::testing::MemoryFileSystem;
  using oxygen::data::AssetType;

  constexpr auto kSceneSource
    = std::string_view { "/Content/Hall.oscene.json" };
  constexpr auto kPhysicsSource
    = std::string_view { "/Content/Hall.physics.json" };
  constexpr auto kScene = std::string_view { "/Content/Hall.oscene" };
  constexpr auto kSidecar = std::string_view { "/Content/Hall.opscene" };

  //! The scene `Hall`: one root node.
  auto HallScene() -> std::vector<std::byte>
  {
    auto hall = fixtures::SceneBuilder { "Hall" };
    static_cast<void>(hall.Node("Root"));
    return hall.Bytes();
  }

  //! `Hall` with another node: a door below its root.
  auto HallSceneWithDoor() -> std::vector<std::byte>
  {
    auto hall = fixtures::SceneBuilder { "Hall" };
    const auto root = hall.Node("Root");
    static_cast<void>(hall.Node("Door", root));
    return hall.Bytes();
  }

  //! `Hall` with the same nodes and other content: a camera on its root.
  auto HallSceneWithCamera() -> std::vector<std::byte>
  {
    auto hall = fixtures::SceneBuilder { "Hall" };
    const auto root = hall.Node("Root");
    hall.PerspectiveCamera(
      root, fixtures::SceneBuilder::DefaultPerspectiveCamera());
    return hall.Bytes();
  }

  //! A cooked root holding the scene `Hall` and the physics sidecar cooked
  //! for it, each from a source of its own.
  class LooseCookedWriterSidecarTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
      LooseCookedWriter writer(root_, storage_);
      WriteScene(writer, original_);
      WriteSidecarFor(writer, original_);
      static_cast<void>(writer.Finish());
    }

    auto WriteScene(LooseCookedWriter& writer,
      const std::span<const std::byte> scene) const -> void
    {
      writer.WriteAssetDescriptor(scene_key_, AssetType::kScene, kScene,
        "Hall.oscene", scene, {}, FromSource(std::string(kSceneSource)));
    }

    //! Writes the sidecar cooked for the scene whose descriptor is `scene`.
    auto WriteSidecarFor(LooseCookedWriter& writer,
      const std::span<const std::byte> scene) const -> void
    {
      writer.WriteAssetDescriptor(sidecar_key_, AssetType::kPhysicsScene,
        kSidecar, "Hall.opscene",
        fixtures::PhysicsSidecarBuilder {}.Bytes(
          scene_key_, scene, "HallPhysics"),
        SceneReference(), FromSource(std::string(kPhysicsSource)));
    }

    [[nodiscard]] auto SceneReference() const -> data::AssetReferences
    {
      return data::AssetReferences::Create({},
        {
          data::KeyReference {
            .key = scene_key_,
            .kind = data::KeyReferenceKind::kLogical,
            .expected_type = AssetType::kScene,
          },
        })
        .value();
    }

    //! The descriptor the root lists for the scene.
    [[nodiscard]] auto ListedScene() const -> std::vector<std::byte>
    {
      return CookedContent::Open(storage_, root_).Descriptor(scene_key_);
    }

    MemoryFileSystem storage_;
    std::filesystem::path root_ = MemoryRoot() / "sidecar_unit";
    data::AssetKey scene_key_ = MakeFirstByteAssetKey(0x41);
    data::AssetKey sidecar_key_ = MakeFirstByteAssetKey(0x42);
    std::vector<std::byte> original_ = HallScene();
    std::vector<std::byte> edited_ = HallSceneWithDoor();
  };

  //! A scene re-cooked with other nodes without its sidecar is refused, and
  //! the root keeps the pair it held.
  NOLINT_TEST_F(
    LooseCookedWriterSidecarTest, SceneWithOtherNodesWithoutSidecarIsRefused)
  {
    LooseCookedWriter writer(root_, storage_);
    WriteScene(writer, edited_);

    EXPECT_THROW(static_cast<void>(writer.Finish()), LooseCookedStaleSidecar);
    EXPECT_EQ(ListedScene(), original_);
  }

  //! A scene re-cooked into the same content keeps its sidecar.
  NOLINT_TEST_F(LooseCookedWriterSidecarTest, UnchangedSceneKeepsItsSidecar)
  {
    LooseCookedWriter writer(root_, storage_);
    WriteScene(writer, original_);

    EXPECT_NO_THROW(static_cast<void>(writer.Finish()));
  }

  //! A scene re-cooked with the same nodes and other content, as when a
  //! scripting sidecar adds its scripts, keeps its sidecar.
  NOLINT_TEST_F(LooseCookedWriterSidecarTest, SceneWithSameNodesKeepsItsSidecar)
  {
    const auto restyled = HallSceneWithCamera();
    ASSERT_NE(restyled, original_);
    LooseCookedWriter writer(root_, storage_);
    WriteScene(writer, restyled);

    EXPECT_NO_THROW(static_cast<void>(writer.Finish()));
    EXPECT_EQ(ListedScene(), restyled);
  }

  //! A scene and the sidecar cooked for its new content commit together.
  NOLINT_TEST_F(LooseCookedWriterSidecarTest, ChangedSceneWithItsSidecarCommits)
  {
    LooseCookedWriter writer(root_, storage_);
    WriteScene(writer, edited_);
    WriteSidecarFor(writer, edited_);

    EXPECT_NO_THROW(static_cast<void>(writer.Finish()));
    EXPECT_EQ(ListedScene(), edited_);
  }

  //! A sidecar cooked for a scene the commit does not list is refused.
  NOLINT_TEST_F(
    LooseCookedWriterSidecarTest, SidecarForAnotherSceneVersionIsRefused)
  {
    LooseCookedWriter writer(root_, storage_);
    WriteSidecarFor(writer, edited_);

    EXPECT_THROW(static_cast<void>(writer.Finish()), LooseCookedStaleSidecar);
  }

  //! Removing a scene and keeping its sidecar is refused; removing both is
  //! not.
  NOLINT_TEST_F(LooseCookedWriterSidecarTest, RemovedSceneTakesItsSidecar)
  {
    {
      LooseCookedWriter writer(root_, storage_);
      writer.RemoveAssetDescriptor(kScene);

      EXPECT_THROW(static_cast<void>(writer.Finish()), LooseCookedStaleSidecar);
    }
    LooseCookedWriter writer(root_, storage_);
    writer.RemoveAssetDescriptor(kScene);
    writer.RemoveAssetDescriptor(kSidecar);

    EXPECT_NO_THROW(static_cast<void>(writer.Finish()));
  }

  //! A sidecar whose scene another cooked root holds is that root's concern.
  NOLINT_TEST_F(LooseCookedWriterSidecarTest, SceneOfAnotherRootIsNotChecked)
  {
    const auto other_scene = MakeFirstByteAssetKey(0x43);
    LooseCookedWriter writer(root_, storage_);
    writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x44),
      AssetType::kPhysicsScene, "/Content/Elsewhere.opscene",
      "Elsewhere.opscene",
      fixtures::PhysicsSidecarBuilder {}.Bytes(
        other_scene, edited_, "ElsewherePhysics"),
      data::AssetReferences::Create({},
        {
          data::KeyReference {
            .key = other_scene,
            .kind = data::KeyReferenceKind::kLogical,
            .expected_type = AssetType::kScene,
          },
        })
        .value(),
      FromSource("/Content/Elsewhere.physics.json"));

    EXPECT_NO_THROW(static_cast<void>(writer.Finish()));
  }

} // namespace

} // namespace oxygen::content::testing
