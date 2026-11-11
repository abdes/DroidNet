//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Jobs/SceneDescriptorImportJob.cpp (physics follows
// its scene), Import/Internal/ScenePhysicsFollowers.cpp

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/SceneSource.h>
#include <Oxygen/Cooker/Import/PhysicsImportSettings.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/ImportHarness.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  using oxygen::cooker::test::HasDiagnosticCode;
  using oxygen::cooker::test::ImportServiceTest;

  constexpr auto kScene = std::string_view { "/Cooked/Scenes/Hall.oscene" };
  constexpr auto kSidecar = std::string_view { "/Cooked/Scenes/Hall.opscene" };

  //! The scene `Hall` with `node_count` nodes, the first the root of the
  //! others.
  auto SceneJson(const int node_count) -> std::string
  {
    auto nodes = std::string {};
    for (int index = 0; index < node_count; ++index) {
      nodes += index == 0 ? "" : ",";
      nodes += R"({"id":"00000000-0000-4000-8000-00000000000)"
        + std::to_string(index + 1) + R"(","name":"Node)"
        + std::to_string(index) + "\""
        + (index == 0 ? std::string {} : R"(,"parent":0)") + "}";
    }
    return R"({"schema_version":)"
      + std::to_string(internal::kSceneDescriptorSchemaVersion)
      + R"(,"name":"Hall","nodes":[)" + nodes + "]}";
  }

  //! A project whose `Content` mount holds the physics source of the scene
  //! `Hall`, cooked into `.cooked`.
  class ScenePhysicsFollowTest : public ImportServiceTest {
  protected:
    void SetUp() override
    {
      ImportServiceTest::SetUp();
      std::filesystem::create_directories(OutputRoot());
      WritePhysicsSource(R"({"bindings":{}})");
    }

    [[nodiscard]] auto Project() const -> std::filesystem::path
    {
      return TempDir() / "follow";
    }

    [[nodiscard]] auto OutputRoot() const -> std::filesystem::path
    {
      return Project() / ".cooked";
    }

    [[nodiscard]] auto PhysicsSource() const -> std::filesystem::path
    {
      return Project() / "Hall.physics.json";
    }

    auto WritePhysicsSource(const std::string_view text) const -> void
    {
      auto file = std::ofstream(PhysicsSource(), std::ios::binary);
      file << text;
    }

    [[nodiscard]] auto SceneRequest(const int node_count) const -> ImportRequest
    {
      auto request = ImportRequest {};
      request.source_path = Project() / "Hall.oscene.json";
      request.job_name = "Hall";
      request.cooked_root = OutputRoot();
      request.asset_virtual_path = std::string(kScene);
      request.source_mount_path
        = cooker::test::MountPathIn(Project(), request.source_path);
      request.source_mounts = cooker::test::ContentMountTable(Project());
      request.scene_descriptor = ImportRequest::SceneDescriptorPayload {
        .descriptor_json = SceneJson(node_count),
      };
      return request;
    }

    [[nodiscard]] auto PhysicsRequest() const -> ImportRequest
    {
      auto request = ImportRequest {};
      request.source_path = PhysicsSource();
      request.job_name = "Hall.physics";
      request.cooked_root = OutputRoot();
      request.source_mount_path
        = cooker::test::MountPathIn(Project(), PhysicsSource());
      request.source_mounts = cooker::test::ContentMountTable(Project());
      request.physics = PhysicsImportSettings {
        .target_scene_virtual_path = std::string(kScene),
        .inline_bindings_json = {},
      };
      return request;
    }

    //! Cooks the scene with one node, then its physics.
    auto CookPair() -> void
    {
      const auto scene = Import(SceneRequest(1));
      ASSERT_HAS_VALUE(scene);
      ASSERT_TRUE(scene->success);
      const auto physics = Import(PhysicsRequest());
      ASSERT_HAS_VALUE(physics);
      ASSERT_TRUE(physics->success);
    }

    [[nodiscard]] auto Content() const -> cooked::CookedContent
    {
      return cooked::CookedContent::Open(
        cooked::RealCookerStorage(), OutputRoot());
    }

    //! The binding the cooked sidecar records of its scene.
    [[nodiscard]] auto SidecarBinding() const
      -> std::optional<data::pak::physics::PhysicsSceneAssetDesc>
    {
      const auto bytes
        = Content().Descriptor(data::AssetKey::FromVirtualPath(kSidecar));
      const auto binding
        = serio::CopyAt<data::pak::physics::PhysicsSceneAssetDesc>(bytes);
      if (!binding) {
        return std::nullopt;
      }
      return *binding;
    }

    //! The node layout hash of the scene the root lists.
    [[nodiscard]] auto ListedSceneLayout() const
      -> data::pak::core::ContentHashDigest
    {
      const auto key = data::AssetKey::FromVirtualPath(kScene);
      return data::ComputeNodeLayoutHash(
        data::SceneAsset(key, Content().Descriptor(key)).GetNodes());
    }

    //! The descriptor of the sidecar the root lists.
    [[nodiscard]] auto SidecarDescriptor() const -> std::vector<std::byte>
    {
      return Content().Descriptor(data::AssetKey::FromVirtualPath(kSidecar));
    }
  };

  //! A physics sidecar cooked from a file records that file.
  NOLINT_TEST_F(ScenePhysicsFollowTest, SidecarRecordsItsOwnSource)
  {
    CookPair();

    const auto content = Content();
    const auto* const sidecar
      = content.FindAsset(data::AssetKey::FromVirtualPath(kSidecar));
    ASSERT_NE(sidecar, nullptr);
    EXPECT_EQ(sidecar->source_path,
      cooker::test::MountPathIn(Project(), PhysicsSource()));
  }

  //! A scene reimported with other nodes takes its physics along: the
  //! sidecar is cooked again for the new scene in the same commit.
  NOLINT_TEST_F(ScenePhysicsFollowTest, ChangedSceneCooksItsPhysicsAgain)
  {
    CookPair();

    const auto report = Import(SceneRequest(2));

    ASSERT_HAS_VALUE(report);
    EXPECT_TRUE(report->success);
    const auto binding = SidecarBinding();
    ASSERT_HAS_VALUE(binding);
    EXPECT_EQ(binding->target_node_count, 2U);
    EXPECT_TRUE(std::ranges::equal(
      binding->target_scene_layout_hash, ListedSceneLayout()));
  }

  //! A scene reimported with the same nodes and other content keeps its
  //! physics as it is: a sidecar binds to its scene's nodes.
  NOLINT_TEST_F(ScenePhysicsFollowTest, SameNodesKeepThePhysics)
  {
    CookPair();
    const auto held = SidecarDescriptor();
    auto renamed = SceneRequest(1);
    ASSERT_HAS_VALUE(renamed.scene_descriptor);
    auto& json = renamed.scene_descriptor->descriptor_json;
    json.replace(json.find("Node0"), 5, "Lobby");

    const auto report = Import(std::move(renamed));

    ASSERT_HAS_VALUE(report);
    EXPECT_TRUE(report->success);
    EXPECT_EQ(SidecarDescriptor(), held);
  }

  //! Physics that no longer cooks refuses the scene reimport, and the root
  //! keeps the pair it held.
  NOLINT_TEST_F(ScenePhysicsFollowTest, PhysicsThatFailsRefusesTheScene)
  {
    CookPair();
    const auto held = ListedSceneLayout();
    WritePhysicsSource("{ not json");

    const auto report = Import(SceneRequest(2));

    ASSERT_HAS_VALUE(report);
    EXPECT_FALSE(report->success);
    EXPECT_TRUE(HasDiagnosticCode(
      report->diagnostics, import_codes::kPhysicsSidecarPayloadParseFailed));
    EXPECT_TRUE(HasDiagnosticCode(
      report->diagnostics, import_codes::kPhysicsSidecarStale));
    EXPECT_EQ(ListedSceneLayout(), held);
  }

  //! A physics source the import cannot locate refuses the scene reimport.
  NOLINT_TEST_F(ScenePhysicsFollowTest, UnlocatedPhysicsSourceRefusesTheScene)
  {
    CookPair();
    auto request = SceneRequest(2);
    request.source_mounts = nullptr;

    const auto report = Import(std::move(request));

    ASSERT_HAS_VALUE(report);
    EXPECT_FALSE(report->success);
    EXPECT_TRUE(HasDiagnosticCode(report->diagnostics,
      import_codes::kSceneDescriptorPhysicsSourceUnlocated));
  }

  //! Physics given inline has no file to cook again from, so a changed scene
  //! is refused until both are reimported together.
  NOLINT_TEST_F(ScenePhysicsFollowTest, InlinePhysicsCannotFollow)
  {
    const auto scene = Import(SceneRequest(1));
    ASSERT_HAS_VALUE(scene);
    ASSERT_TRUE(scene->success);
    auto inline_physics = PhysicsRequest();
    inline_physics.source_path = "inline://physics-sidecar";
    inline_physics.source_mount_path.clear();
    ASSERT_HAS_VALUE(inline_physics.physics);
    inline_physics.physics->inline_bindings_json = R"({"bindings":{}})";
    const auto physics = Import(std::move(inline_physics));
    ASSERT_HAS_VALUE(physics);
    ASSERT_TRUE(physics->success);

    const auto report = Import(SceneRequest(2));

    ASSERT_HAS_VALUE(report);
    EXPECT_FALSE(report->success);
    EXPECT_TRUE(HasDiagnosticCode(
      report->diagnostics, import_codes::kPhysicsSidecarStale));
  }

} // namespace

} // namespace oxygen::content::import::test
