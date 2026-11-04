//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Jobs/SceneDescriptorImportJob.cpp

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Span.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakBuilder.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/CookedFiles.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/ImportHarness.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Cooker/testing/AssetDescriptors.h>
#include <Oxygen/Cooker/testing/DescriptorDefects.h>
#include <Oxygen/Cooker/testing/GeometryBuilder.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/CameraAspectMode.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/StableId.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::base::CheckedAt;

namespace oxygen::content::import::test {

namespace {

  using nlohmann::json;
  using oxygen::cooker::test::CookedFile;
  using oxygen::cooker::test::HasCookedFile;
  using oxygen::cooker::test::ImportServiceTest;
  using oxygen::cooker::test::ReadBytes;
  using oxygen::cooker::test::SceneDescriptor;
  using oxygen::cooker::test::WriteText;
  using oxygen::cooker::testing::GeometryBuilder;
  using oxygen::cooker::testing::LooseCookedRoot;
  using oxygen::cooker::testing::MaterialBuilder;
  using oxygen::cooker::testing::Materialize;
  using oxygen::cooker::testing::SeedBytes;

  using oxygen::cooker::test::HasDiagnosticCode;

  constexpr auto kEngineCube
    = std::string_view { "/Engine/Generated/BasicShapes/Cube.ogeo" };
  constexpr auto kEngineDefault
    = std::string_view { "/Engine/Generated/Materials/Default.omat" };

  class SceneDescriptorImportJobTest : public ImportServiceTest {
  protected:
    //! A request that cooks `scene` at its own identity.
    auto MakeRequest(const std::filesystem::path& cooked_root,
      const SceneDescriptor& scene) -> ImportRequest
    {
      const auto& document = scene.Json();
      auto request = ImportRequest {};
      request.source_path = "inline://scene.oscene.json";
      request.asset_virtual_path = scene.Identity();
      request.source_mount_path
        = oxygen::cooker::test::SourceMountPath(request.source_path);
      request.cooked_root = cooked_root;
      request.scene_descriptor = ImportRequest::SceneDescriptorPayload {
        .descriptor_json = document.dump(),
      };
      return request;
    }
  };

  NOLINT_TEST_F(SceneDescriptorImportJobTest, EmptyScenePreservesEnvironment)
  {

    const auto root = TempDir() / "empty_scene_environment";
    std::filesystem::create_directories(root);
    auto document = SceneDescriptor { "/Cooked/Scenes/EmptyScene.oscene" };
    document.Set("/nodes", json::array())
      .Set("/environment/background/color_rgb", json::array({ 0.1, 0.2, 0.3 }))
      .Set("/environment/post_process_volume/manual_exposure_ev", 9.5);
    const auto report = Import(MakeRequest(root, document));
    ASSERT_HAS_VALUE(report);
    ASSERT_TRUE(report->success);
    ASSERT_EQ(report->scenes_written, 1U);
    const auto bytes = ReadBytes(CookedFile(root, "Scenes/EmptyScene.oscene"));
    const auto scene = data::SceneAsset(data::AssetKey {}, bytes);
    EXPECT_TRUE(scene.GetNodes().empty());
    EXPECT_TRUE(
      scene.GetComponents<data::pak::world::RenderableRecord>().empty());
    EXPECT_TRUE(
      scene.GetComponents<data::pak::world::PerspectiveCameraRecord>().empty());
    const auto background = scene.TryGetBackgroundEnvironment();
    if (!background) {
      FAIL() << "Expected background";
    }
    EXPECT_FLOAT_EQ(background->color_rgb[0], 0.1F);
    EXPECT_FLOAT_EQ(background->color_rgb[1], 0.2F);
    EXPECT_FLOAT_EQ(background->color_rgb[2], 0.3F);
    const auto exposure = scene.TryGetPostProcessVolumeEnvironment();
    if (!exposure) {
      FAIL() << "Expected exposure";
    }
    EXPECT_FLOAT_EQ(exposure->manual_exposure_ev, 9.5F);
  }

  NOLINT_TEST_F(SceneDescriptorImportJobTest, EmptySceneRejectsDanglingCamera)
  {

    const auto root = TempDir() / "empty_scene_invalid_camera";
    std::filesystem::create_directories(root);
    auto document = SceneDescriptor { "/Cooked/Scenes/EmptyScene.oscene" };
    document.Set("/nodes", json::array())
      .Set("/cameras/perspective",
        json::array({ json { { "node", 0 }, { "aspect_mode", "auto" } } }));
    const auto report = Import(MakeRequest(root, document));
    ASSERT_HAS_VALUE(report);
    EXPECT_FALSE(report->success);
    EXPECT_EQ(report->scenes_written, 0U);
    EXPECT_TRUE(HasDiagnosticCode(report->diagnostics,
      import_codes::kSceneDescriptorCameraNodeIndexOutOfRange));
  }

  //! Writes a cooked texture sidecar naming source texture `index`.
  //! An authored scene records its instances and start state, and lists the
  //! instanced scene and starting contexts as references.
  NOLINT_TEST_F(SceneDescriptorImportJobTest, RecordsInstancesAndStartState)
  {
    namespace world = data::pak::world;
    const auto root = TempDir() / "composed_scene";
    // The starting context is cooked: an index lists it.
    auto inputs = LooseCookedRoot { "Cooked" };
    static_cast<void>(inputs.InputContext("Input/gameplay.oimap",
      oxygen::cooker::testing::InputContextBuilder { "Gameplay" }));
    Materialize(inputs.Files(), root);
    auto model_document = SceneDescriptor { "/Cooked/Scenes/Model.oscene" };
    model_document.Set("/nodes/0/name", "part");
    const auto model = Import(MakeRequest(root, model_document));
    ASSERT_HAS_VALUE(model);
    ASSERT_TRUE(model->success);

    auto level = SceneDescriptor { "/Cooked/Scenes/Level.oscene" };
    const auto instance = level.Node("model", 0U);
    level.Node("camera", 0U);
    level.Set("/nodes/" + std::to_string(instance) + "/instance",
      json { { "scene_ref", level.Spell("/Cooked/Scenes/Model.oscene") } });
    level.Set("/cameras/perspective",
      json::array({ json { { "node", 2 }, { "aspect_mode", "auto" } } }));
    level.Set("/starting_camera/node", 2);
    level.Append("/starting_input_contexts",
      json {
        { "context_ref", level.Spell("/Cooked/Input/gameplay.oimap") },
        { "priority", 3 },
      });
    const auto report = Import(MakeRequest(root, level));
    ASSERT_HAS_VALUE(report);
    ASSERT_TRUE(report->success);

    const auto model_key
      = data::AssetKey::FromVirtualPath("/Cooked/Scenes/Model.oscene");
    const auto context_key
      = data::AssetKey::FromVirtualPath("/Cooked/Input/gameplay.oimap");
    const auto bytes = ReadBytes(CookedFile(root, "Scenes/Level.oscene"));
    const auto scene = data::SceneAsset(data::AssetKey {}, bytes);
    EXPECT_FALSE(scene.IsModelScene());
    EXPECT_EQ(scene.GetStartingCamera(),
      std::optional { world::NodeRef { .node_index = 2U } });
    const auto instances = scene.GetComponents<world::SceneInstanceRecord>();
    ASSERT_EQ(instances.size(), 1U);
    EXPECT_EQ(CheckedAt(instances, 0).node_index, 1U);
    EXPECT_EQ(CheckedAt(instances, 0).scene_key, model_key);
    const auto contexts
      = scene.GetComponents<world::StartingInputContextRecord>();
    ASSERT_EQ(contexts.size(), 1U);
    EXPECT_EQ(CheckedAt(contexts, 0).context_key, context_key);
    EXPECT_EQ(CheckedAt(contexts, 0).priority, 3);

    const auto index = lc::LooseCookedIndex::LoadFromRoot(root);
    const auto scene_key
      = index.FindAssetKeyByVirtualPath("/Cooked/Scenes/Level.oscene");
    ASSERT_HAS_VALUE(scene_key);
    const auto references = index.FindAssetReferences(*scene_key);
    ASSERT_HAS_VALUE(references);
    EXPECT_THAT(references->Keys(),
      ::testing::UnorderedElementsAre(
        data::KeyReference {
          .key = model_key,
          .kind = data::KeyReferenceKind::kAsset,
          .expected_type = data::AssetType::kScene,
        },
        data::KeyReference {
          .key = context_key,
          .kind = data::KeyReferenceKind::kAsset,
          .expected_type = data::AssetType::kInputMappingContext,
        }));
  }

  //! The starting camera must own a camera.
  NOLINT_TEST_F(
    SceneDescriptorImportJobTest, RejectsStartingCameraWithoutCamera)
  {
    const auto root = TempDir() / "starting_camera_without_camera";
    std::filesystem::create_directories(root);
    auto document = SceneDescriptor { "/Cooked/Scenes/Scene.oscene" };
    document.Set("/starting_camera/node", 0);
    const auto report = Import(MakeRequest(root, document));
    ASSERT_HAS_VALUE(report);
    EXPECT_FALSE(report->success);
    EXPECT_TRUE(HasDiagnosticCode(
      report->diagnostics, import_codes::kSceneStartingCameraNotACamera));
  }

  //! A scene that instances itself, directly or through another scene, fails.
  NOLINT_TEST_F(SceneDescriptorImportJobTest, RejectsInstanceCycles)
  {
    const auto root = TempDir() / "instance_cycles";
    std::filesystem::create_directories(root);
    // A one-node scene, optionally instancing another scene on its node.
    const auto scene = [](const std::string_view name,
                         const std::string_view instanced) -> SceneDescriptor {
      auto document = SceneDescriptor {
        "/Cooked/Scenes/" + std::string { name } + ".oscene",
      };
      if (!instanced.empty()) {
        document.Set("/nodes/0/instance",
          json {
            {
              "scene_ref",
              document.Spell(
                "/Cooked/Scenes/" + std::string(instanced) + ".oscene"),
            },
          });
      }
      return document;
    };
    const auto cook = [&](const std::string_view name,
                        const std::string_view instanced) -> bool {
      const auto report = Import(MakeRequest(root, scene(name, instanced)));
      return report.has_value() && report->success;
    };
    ASSERT_TRUE(cook("A", ""));
    ASSERT_TRUE(cook("B", "A"));

    for (const auto* const instanced : { "A", "B" }) {
      const auto report = Import(MakeRequest(root, scene("A", instanced)));
      ASSERT_HAS_VALUE(report);
      EXPECT_FALSE(report->success) << instanced;
      EXPECT_TRUE(HasDiagnosticCode(
        report->diagnostics, import_codes::kSceneDescriptorInstanceCycle))
        << instanced;
    }
  }

  //! Changes of a placed rig: one that resolves is cooked; one naming no
  //! node of the rig and a starting camera naming a node without a camera
  //! are warnings, left out, and the cook succeeds (scene instances 7.9,
  //! 7.10).
  NOLINT_TEST_F(SceneDescriptorImportJobTest, LeavesOutChangesThatDoNotResolve)
  {
    namespace world = data::pak::world;
    const auto root = TempDir() / "unresolved_changes";
    std::filesystem::create_directories(root);
    // The writer gives node i the ID ending in i + 1.
    constexpr auto kEye
      = std::string_view { "00000000-0000-4000-8000-000000000002" };
    constexpr auto kStranger
      = std::string_view { "00000000-0000-4000-8000-000000000009" };
    auto rig_document = SceneDescriptor { "/Cooked/Scenes/Rig.oscene" };
    rig_document.Set("/nodes/0/name", "rig");
    rig_document.Node("eye", 0U);
    const auto rig = Import(MakeRequest(root, rig_document));
    ASSERT_HAS_VALUE(rig);
    ASSERT_TRUE(rig->success);
    const auto change = [](const std::string_view node) -> nlohmann::json {
      auto components = nlohmann::json::object();
      components["transform"]["values"]["/local_position/0"] = 2.5;
      return {
        { "path", nlohmann::json::array({ node }) },
        { "components", std::move(components) },
      };
    };
    auto level = SceneDescriptor { "/Cooked/Scenes/Level.oscene" };
    const auto rig_node = level.Node("rig", 0U);
    level
      .Set("/nodes/" + std::to_string(rig_node) + "/instance",
        json { { "scene_ref", level.Spell("/Cooked/Scenes/Rig.oscene") } })
      .Set("/nodes/1/instance/overrides",
        nlohmann::json::array({ change(kEye), change(kStranger) }))
      .Set("/starting_camera",
        json { { "node", 1 }, { "path", nlohmann::json::array({ kEye }) } });

    const auto report = Import(MakeRequest(root, level));

    ASSERT_HAS_VALUE(report);
    ASSERT_TRUE(report->success);
    EXPECT_TRUE(HasDiagnosticCode(report->diagnostics,
      import_codes::kSceneDescriptorInstanceOverrideUnresolved));
    EXPECT_TRUE(HasDiagnosticCode(
      report->diagnostics, import_codes::kSceneStartingCameraUnresolved));
    const auto scene = data::SceneAsset(
      data::AssetKey {}, ReadBytes(CookedFile(root, "Scenes/Level.oscene")));
    EXPECT_FALSE(scene.GetStartingCamera().has_value());
    const auto changes = scene.GetComponents<world::OverrideRecord>();
    ASSERT_EQ(changes.size(), 1U);
    const auto eye = data::NodeId::FromString(kEye);
    ASSERT_HAS_VALUE(eye);
    EXPECT_EQ(
      scene.GetNodePath(CheckedAt(changes, 0).target), std::vector { *eye });
  }

  //! Describes the metering mask `Textures/Meter.otex` as a 1x1 texture of
  //! `format` into `root`, naming the row it holds, or the reserved row zero
  //! when `reserved_row`. Returns the row the texture lives in.
  auto MaterializeMeteringMask(const std::filesystem::path& root,
    const Format format, const bool reserved_row = false) -> ResourceIndexT
  {
    auto textures = LooseCookedRoot { "Cooked" };
    auto desc = LooseCookedRoot::OnePixelTexture();
    desc.format = static_cast<uint8_t>(format);
    const auto row = textures.AddTexture(SeedBytes(1U), desc);
    textures.DescribeTexture(
      "Textures/Meter.otex", reserved_row ? ResourceIndexT { 0U } : row);
    Materialize(textures.Files(), root);
    return row;
  }

  //! Adds a 1x1 RGBA16F cubemap to `textures`, described at `leaf`.
  auto AddCubemap(LooseCookedRoot& textures, const std::string_view leaf)
    -> ResourceIndexT
  {
    auto desc = LooseCookedRoot::OnePixelTexture();
    desc.texture_type = static_cast<uint8_t>(TextureType::kTextureCube);
    desc.array_layers = kCubeFaceCount;
    desc.format = static_cast<uint8_t>(Format::kRGBA16Float);
    const auto row = textures.AddTexture(SeedBytes(1U, 24U), desc);
    textures.DescribeTexture(leaf, row);
    return row;
  }

  NOLINT_TEST_F(SceneDescriptorImportJobTest,
    MeteringMaskUsesCurrentSourceLocalTextureReference)
  {

    const auto root = TempDir() / "mask_reference";
    std::filesystem::create_directories(root);
    auto mask_scene = SceneDescriptor { "/Cooked/Scenes/MaskScene.oscene" };
    mask_scene.Reference(
      "/environment/post_process_volume/auto_exposure_metering_mask",
      "/Cooked/Textures/Meter.otex");
    const auto mask_row = MaterializeMeteringMask(root, Format::kRGBA8UNorm);
    const auto success = Import(MakeRequest(root, mask_scene));
    ASSERT_HAS_VALUE(success);
    ASSERT_TRUE(success->success);
    const auto bytes = ReadBytes(CookedFile(root, "Scenes/MaskScene.oscene"));
    const auto scene = data::SceneAsset(data::AssetKey {}, bytes);
    const auto exposure = scene.TryGetPostProcessVolumeEnvironment();
    ASSERT_HAS_VALUE(exposure) << "Expected exposure to contain a value";
    EXPECT_EQ(exposure->auto_exposure_metering_mask.get(), 0U);
    const auto index = lc::LooseCookedIndex::LoadFromRoot(root);
    const auto scene_key
      = index.FindAssetKeyByVirtualPath("/Cooked/Scenes/MaskScene.oscene");
    ASSERT_HAS_VALUE(scene_key);
    const auto references = index.FindAssetReferences(*scene_key);
    ASSERT_HAS_VALUE(references);
    EXPECT_THAT(references->Resources(),
      ::testing::ElementsAre(data::ResourceBinding {
        .kind = data::ResourceKind::kTexture,
        .index = mask_row,
      }));

    MaterializeMeteringMask(root, Format::kRGBA8UNormSRGB);
    const auto srgb = Import(MakeRequest(root, mask_scene));
    ASSERT_HAS_VALUE(srgb);
    EXPECT_FALSE(srgb->success);
    EXPECT_TRUE(HasDiagnosticCode(
      srgb->diagnostics, import_codes::kSceneDescriptorMaskFormatInvalid));
    // A descriptor that names no table row is a root its index cannot vouch
    // for: opening it refuses the root.
    MaterializeMeteringMask(root, Format::kRGBA8UNorm, true);
    const auto absent = Import(MakeRequest(root, mask_scene));
    ASSERT_HAS_VALUE(absent);
    EXPECT_FALSE(absent->success);
    EXPECT_TRUE(HasDiagnosticCode(
      absent->diagnostics, import_codes::kSceneDescriptorIndexLoadFailed));

    // A deleted mask, a root that keeps the texture but no longer describes
    // it, leaves the scene without one, with a warning.
    auto undescribed = LooseCookedRoot { "Cooked" };
    static_cast<void>(undescribed.AddTexture(SeedBytes(1U)));
    Materialize(undescribed.Files(), root);
    std::filesystem::remove(root / "Textures/Meter.otex");
    const auto deleted = Import(MakeRequest(root, mask_scene));
    ASSERT_HAS_VALUE(deleted);
    EXPECT_TRUE(deleted->success);
    EXPECT_TRUE(HasDiagnosticCode(deleted->diagnostics,
      import_codes::kSceneDescriptorTextureDescriptorMissing));
    const auto unmasked = data::SceneAsset(
      data::AssetKey {}, ReadBytes(CookedFile(root, "Scenes/MaskScene.oscene")))
                            .TryGetPostProcessVolumeEnvironment();
    ASSERT_HAS_VALUE(unmasked);
    EXPECT_EQ(
      unmasked->auto_exposure_metering_mask, data::kNoResourceReference);
  }

  //! A mask a cooked context root holds binds by key, and its format is still
  //! checked.
  NOLINT_TEST_F(
    SceneDescriptorImportJobTest, MeteringMaskFromAContextRootBindsByKey)
  {
    // Arrange
    const auto root = TempDir() / "context_mask_reference";
    const auto library = TempDir() / "mask_library";
    std::filesystem::create_directories(root);
    auto library_root = LooseCookedRoot { "Library" };
    const auto mask_row = library_root.AddTexture(SeedBytes(1U));
    const auto kMask
      = library_root.DescribeTexture("Textures/Meter.otex", mask_row);
    Materialize(library_root.Files(), library);
    auto mask_scene = SceneDescriptor { "/Cooked/Scenes/MaskScene.oscene" };
    mask_scene.Reference(
      "/environment/post_process_volume/auto_exposure_metering_mask",
      "/Library/Textures/Meter.otex");
    auto request = MakeRequest(root, mask_scene);
    request.cooked_context_roots = { library };

    // Act
    const auto report = Import(std::move(request));

    // Assert
    ASSERT_HAS_VALUE(report);
    ASSERT_TRUE(report->success);
    const auto index = lc::LooseCookedIndex::LoadFromRoot(root);
    const auto scene_key
      = index.FindAssetKeyByVirtualPath("/Cooked/Scenes/MaskScene.oscene");
    ASSERT_HAS_VALUE(scene_key);
    const auto references = index.FindAssetReferences(*scene_key);
    ASSERT_HAS_VALUE(references);
    const auto exposure = data::SceneAsset(
      data::AssetKey {}, ReadBytes(CookedFile(root, "Scenes/MaskScene.oscene")))
                            .TryGetPostProcessVolumeEnvironment();
    ASSERT_HAS_VALUE(exposure);
    const auto target
      = references->ResolveTexture(exposure->auto_exposure_metering_mask);
    ASSERT_HAS_VALUE(target);
    EXPECT_EQ(std::get<data::AssetKey>(*target),
      data::TextureKeyFromVirtualPath(kMask));
  }

  NOLINT_TEST_F(SceneDescriptorImportJobTest,
    PhysicalCamerasRoundTripWithAuthoredValuesAndDefaults)
  {
    namespace world = data::pak::world;

    const auto root = TempDir() / "physical_cameras";
    std::filesystem::create_directories(root);
    auto physical = SceneDescriptor { "/Cooked/Scenes/Physical.oscene" };
    for (const auto* const name : { "Camera1", "Camera2", "Camera3" }) {
      physical.Node(name);
    }
    physical
      .Set("/cameras/perspective",
        json::array({
          json {
            { "node", 0 },
            { "aspect_mode", "fixed" },
            { "aspect_ratio", 1.5 },
            { "aperture_f", 2.8 },
            { "shutter_rate", 250 },
            { "iso", 400 },
          },
          json { { "node", 1 }, { "aspect_mode", "auto" } },
        }))
      .Set("/cameras/orthographic",
        json::array({
          json {
            { "node", 2 },
            { "aspect_mode", "fixed" },
            { "orthographic_size", 2 },
            { "aspect_ratio", 1.5 },
            { "aperture_f", 8 },
            { "shutter_rate", 60 },
            { "iso", 200 },
          },
          json { { "node", 3 }, { "aspect_mode", "auto" } },
        }));
    const auto report = Import(MakeRequest(root, physical));
    ASSERT_HAS_VALUE(report);
    ASSERT_TRUE(report->success);
    const auto bytes = ReadBytes(CookedFile(root, "Scenes/Physical.oscene"));
    const auto scene = data::SceneAsset(data::AssetKey {}, bytes);
    const auto perspective
      = scene.GetComponents<world::PerspectiveCameraRecord>();
    const auto orthographic
      = scene.GetComponents<world::OrthographicCameraRecord>();
    ASSERT_EQ(perspective.size(), 2U);
    ASSERT_EQ(orthographic.size(), 2U);
    EXPECT_FLOAT_EQ(CheckedAt(perspective, 0).aperture_f, 2.8F);
    EXPECT_FLOAT_EQ(CheckedAt(perspective, 0).shutter_rate, 250.0F);
    EXPECT_FLOAT_EQ(CheckedAt(perspective, 0).iso, 400.0F);
    EXPECT_EQ(CheckedAt(perspective, 0).aspect_mode, CameraAspectMode::kFixed);
    EXPECT_FLOAT_EQ(CheckedAt(perspective, 0).aspect_ratio, 1.5F);
    EXPECT_EQ(CheckedAt(perspective, 1).aspect_mode, CameraAspectMode::kAuto);
    EXPECT_EQ(CheckedAt(orthographic, 0).aspect_mode, CameraAspectMode::kFixed);
    EXPECT_EQ(CheckedAt(orthographic, 1).aspect_mode, CameraAspectMode::kAuto);
    EXPECT_FLOAT_EQ(CheckedAt(orthographic, 0).left, -3.0F);
    EXPECT_FLOAT_EQ(CheckedAt(orthographic, 0).right, 3.0F);
    EXPECT_FLOAT_EQ(CheckedAt(orthographic, 0).bottom, -2.0F);
    EXPECT_FLOAT_EQ(CheckedAt(orthographic, 0).top, 2.0F);
    EXPECT_FLOAT_EQ(CheckedAt(orthographic, 0).aperture_f, 8.0F);
    EXPECT_FLOAT_EQ(CheckedAt(orthographic, 0).shutter_rate, 60.0F);
    EXPECT_FLOAT_EQ(CheckedAt(orthographic, 0).iso, 200.0F);
    EXPECT_FLOAT_EQ(CheckedAt(perspective, 1).aperture_f, 11.0F);
    EXPECT_FLOAT_EQ(CheckedAt(perspective, 1).shutter_rate, 125.0F);
    EXPECT_FLOAT_EQ(CheckedAt(perspective, 1).iso, 100.0F);
    EXPECT_FLOAT_EQ(CheckedAt(orthographic, 1).aperture_f, 11.0F);
    EXPECT_FLOAT_EQ(CheckedAt(orthographic, 1).shutter_rate, 125.0F);
    EXPECT_FLOAT_EQ(CheckedAt(orthographic, 1).iso, 100.0F);
  }

  NOLINT_TEST_F(
    SceneDescriptorImportJobTest, RejectsCoupledExposureAndCurveErrors)
  {

    const auto root = TempDir() / "invalid_exposure";
    std::filesystem::create_directories(root);
    for (const auto& invalid : std::vector<nlohmann::json> {
           { { "auto_exposure_min_ev", 10 }, { "auto_exposure_max_ev", 5 } },
           {
             { "auto_exposure_low_percentile", 0.9 },
             { "auto_exposure_high_percentile", 0.1 },
           },
           {
             { "auto_exposure_min_log_luminance", 31 },
             { "auto_exposure_log_luminance_range", 2 },
           },
           {
             {
               "auto_exposure_compensation_curve",
               {
                 { { "metered_ev", 0 }, { "compensation_ev", 1 } },
                 { { "metered_ev", 0 }, { "compensation_ev", 2 } },
               },
             },
           },
           { { "exposure_compensation_ev", 10000 } },
         }) {
      auto document = SceneDescriptor { "/Cooked/Scenes/Invalid.oscene" };
      document.Set("/environment/post_process_volume", invalid);
      SCOPED_TRACE(document.Text());
      const auto report = Import(MakeRequest(root, document));
      ASSERT_HAS_VALUE(report);
      EXPECT_FALSE(report->success);
      EXPECT_EQ(report->scenes_written, 0U);
      EXPECT_TRUE(HasDiagnosticCode(
        report->diagnostics, import_codes::kSceneDescriptorExposureInvalid));
    }
  }

  NOLINT_TEST_F(
    SceneDescriptorImportJobTest, NodeFlagSourceModesRoundTripWithoutFlattening)
  {
    namespace world = data::pak::world;

    const auto root = TempDir() / "node_flag_source_modes";
    std::filesystem::create_directories(root);
    auto flags = SceneDescriptor { "/Cooked/Scenes/Flags.oscene" };
    flags.Set("/nodes/0/name", "HiddenRoot")
      .Set("/nodes/0/flags",
        json {
          { "visible", "hidden" },
          { "casts_shadows", "off" },
          { "receives_shadows", "on" },
        });
    flags.Node("InheritedChild", 0U);
    const auto local_child = flags.Node("LocalChild", 0U);
    flags.Set("/nodes/" + std::to_string(local_child) + "/flags",
      json {
        { "visible", "shown" },
        { "casts_shadows", "on" },
        { "receives_shadows", "off" },
        { "static", true },
      });
    flags.Node("DefaultRoot");
    const auto inherited_root = flags.Node("InheritedRoot");
    flags.Set("/nodes/" + std::to_string(inherited_root) + "/flags",
      json {
        { "visible", "inherit" },
        { "casts_shadows", "inherit" },
        { "receives_shadows", "inherit" },
      });
    const auto report = Import(MakeRequest(root, flags));
    ASSERT_HAS_VALUE(report);
    ASSERT_TRUE(report->success);
    ASSERT_EQ(report->scenes_written, 1U);
    const auto bytes = ReadBytes(CookedFile(root, "Scenes/Flags.oscene"));
    const auto scene = data::SceneAsset(data::AssetKey {}, bytes);
    const auto nodes = scene.GetNodes();
    ASSERT_EQ(nodes.size(), 5U);
    // Ray cast selection is on unless a descriptor turns it off.
    constexpr auto kSelectable = world::kSceneNodeFlag_RayCastingSelectable;
    EXPECT_EQ(CheckedAt(nodes, 0).node_flags,
      world::kSceneNodeFlag_ReceivesShadows | kSelectable);
    EXPECT_EQ(CheckedAt(nodes, 0).inherited_flags, 0U);
    EXPECT_EQ(CheckedAt(nodes, 1).node_flags, kSelectable);
    EXPECT_EQ(
      CheckedAt(nodes, 1).inherited_flags, world::kSceneNodeFlags_Inheritable);
    EXPECT_EQ(CheckedAt(nodes, 2).node_flags,
      world::kSceneNodeFlag_Visible | world::kSceneNodeFlag_CastsShadows
        | world::kSceneNodeFlag_Static | kSelectable);
    EXPECT_EQ(CheckedAt(nodes, 2).inherited_flags, 0U);
    EXPECT_EQ(CheckedAt(nodes, 3).node_flags,
      world::kSceneNodeFlags_Inheritable | kSelectable);
    EXPECT_EQ(CheckedAt(nodes, 3).inherited_flags, 0U);
    EXPECT_EQ(CheckedAt(nodes, 4).node_flags, kSelectable);
    EXPECT_EQ(
      CheckedAt(nodes, 4).inherited_flags, world::kSceneNodeFlags_Inheritable);
  }

  auto WriteIndexedReference(const std::filesystem::path& root,
    const data::AssetKey& key, const data::AssetType type) -> void
  {
    auto writer
      = LooseCookedWriter(root, oxygen::content::cooked::RealCookerStorage());
    auto geometry = GeometryBuilder { "Mesh" };
    geometry.Submesh(geometry.ProceduralLod(),
      data::SurfaceId::FromStableIdentity("fixture-surface"));
    const auto material = MaterialBuilder { "Wrong" };
    const auto is_geometry = type == data::AssetType::kGeometry;
    writer.WriteAssetDescriptor(key, type, "/Art/Geometry/Mesh.ogeo",
      is_geometry ? "Geometry/Mesh.ogeo" : "Materials/Wrong.omat",
      is_geometry ? geometry.Bytes() : material.Bytes(),
      is_geometry ? geometry.References() : material.References(),
      content::import::LooseCookedProducer {
        .source = "/Art/Geometry/Mesh.ogeo.json",
        .completes = {},
      });
    static_cast<void>(writer.Finish());
  }

  NOLINT_TEST_F(
    SceneDescriptorImportJobTest, OrderedRootsResolveLastMatchingAsset)
  {

    const auto output = TempDir() / "ordered_output";
    std::filesystem::create_directories(output);
    const auto library = TempDir() / "ordered_library";
    std::filesystem::create_directories(library);
    const auto own_key = data::AssetKey::FromVirtualPath("/Own/Mesh.ogeo");
    const auto library_key
      = data::AssetKey::FromVirtualPath("/Library/Mesh.ogeo");
    WriteIndexedReference(output, own_key, data::AssetType::kGeometry);
    WriteIndexedReference(library, library_key, data::AssetType::kGeometry);
    auto descriptor = SceneDescriptor { "/Art/Scenes/Scene.oscene" };
    descriptor.Set("/nodes/0/name", "Mesh")
      .Renderable(0, "/Art/Geometry/Mesh.ogeo");
    for (const auto own_wins : { false, true }) {
      auto request = MakeRequest(output, descriptor);
      request.cooked_context_roots = own_wins
        ? std::vector<std::filesystem::path> { library, output }
        : std::vector<std::filesystem::path> { output, library };
      const auto report = Import(std::move(request));
      ASSERT_HAS_VALUE(report);
      ASSERT_TRUE(report->success);
      const auto bytes = ReadBytes(CookedFile(output, "Scenes/Scene.oscene"));
      const auto scene = data::SceneAsset(
        data::AssetKey {}, std::span<const std::byte>(bytes));
      const auto renderables
        = scene.GetComponents<data::pak::world::RenderableRecord>();
      ASSERT_EQ(renderables.size(), 1U);
      EXPECT_EQ(CheckedAt(renderables, 0).geometry_key,
        own_wins ? own_key : library_key);
    }
  }

  NOLINT_TEST_F(SceneDescriptorImportJobTest, UnindexedDescriptorFileIsNoAsset)
  {

    const auto output = TempDir() / "unindexed_output";
    std::filesystem::create_directories(output);
    const auto library = TempDir() / "unindexed_library";
    std::filesystem::create_directories(library);
    const auto library_key
      = data::AssetKey::FromVirtualPath("/Library/Mesh.ogeo");
    // Only an index lists an asset; the file at the identity is none.
    WriteText(output / "Geometry/Mesh.ogeo", "stray descriptor");
    WriteIndexedReference(library, library_key, data::AssetType::kGeometry);
    auto descriptor = SceneDescriptor { "/Art/Scenes/Scene.oscene" };
    descriptor.Set("/nodes/0/name", "Mesh")
      .Renderable(0, "/Art/Geometry/Mesh.ogeo");
    auto request = MakeRequest(output, descriptor);
    request.cooked_context_roots
      = std::vector<std::filesystem::path> { library, output };
    const auto report = Import(std::move(request));
    ASSERT_HAS_VALUE(report);
    ASSERT_TRUE(report->success)
      << oxygen::cooker::test::DiagnosticSummary(report->diagnostics);
    const auto bytes = ReadBytes(CookedFile(output, "Scenes/Scene.oscene"));
    const auto scene
      = data::SceneAsset(data::AssetKey {}, std::span<const std::byte>(bytes));
    const auto renderables
      = scene.GetComponents<data::pak::world::RenderableRecord>();
    ASSERT_EQ(renderables.size(), 1U);
    EXPECT_EQ(CheckedAt(renderables, 0).geometry_key, library_key);
  }

  NOLINT_TEST_F(
    SceneDescriptorImportJobTest, WinningRootTypeMismatchDoesNotFallBack)
  {

    const auto output = TempDir() / "wrong_winner_output";
    std::filesystem::create_directories(output);
    const auto library = TempDir() / "wrong_winner_library";
    std::filesystem::create_directories(library);
    WriteIndexedReference(output,
      data::AssetKey::FromVirtualPath("/Own/Mesh.ogeo"),
      data::AssetType::kGeometry);
    WriteIndexedReference(library,
      data::AssetKey::FromVirtualPath("/Library/Wrong.omat"),
      data::AssetType::kMaterial);
    auto scene_document = SceneDescriptor { "/Cooked/Scenes/Scene.oscene" };
    scene_document.Set("/nodes/0/name", "Mesh")
      .Renderable(0, "/Art/Geometry/Mesh.ogeo");
    auto request = MakeRequest(output, scene_document);
    request.cooked_context_roots = { library };
    const auto report = Import(std::move(request));
    ASSERT_HAS_VALUE(report);
    EXPECT_FALSE(report->success);
    EXPECT_TRUE(HasDiagnosticCode(report->diagnostics,
      import_codes::kSceneDescriptorReferenceTypeMismatch));
  }

  NOLINT_TEST_F(
    SceneDescriptorImportJobTest, ResolvesReferencesAndEmitsSceneDescriptor)
  {

    const auto cooked_root = TempDir() / "resolves_and_emits";
    // Every asset the scene names is cooked: an index lists it.
    auto cooked = LooseCookedRoot { "Cooked" };
    auto cube = GeometryBuilder { "Cube" };
    const auto surface
      = data::SurfaceId::FromString("00000000-0000-0000-0000-000000000001");
    ASSERT_HAS_VALUE(surface);
    cube.Submesh(cube.ProceduralLod(), *surface);
    static_cast<void>(cooked.Geometry("Geometry/cube.ogeo", cube));
    static_cast<void>(cooked.Material("Materials/cube.omat"));
    static_cast<void>(cooked.Script("Scripts/spin.oscript"));
    static_cast<void>(cooked.InputAction("Input/jump.oiact"));
    static_cast<void>(cooked.InputContext("Input/gameplay.oimap",
      oxygen::cooker::testing::InputContextBuilder { "Gameplay" }));
    const auto physics_scene = cooked.Scene("Scenes/PhysicsScene.oscene");
    static_cast<void>(
      cooked.PhysicsSidecar("Scenes/PhysicsScene.opscene", physics_scene));
    Materialize(cooked.Files(), cooked_root);

    auto demo = SceneDescriptor { "/Cooked/Scenes/DemoScene.oscene" };
    auto surface_values = json::object();
    surface_values["/material"] = demo.Spell("/Cooked/Materials/cube.omat");
    surface_values["/visible"] = false;
    surface_values["/casts_shadows"] = true;
    auto lod_policy = json {
      { "type", "distance" },
      { "thresholds", json::array({ 8 }) },
      { "hysteresis_ratio", 0.1 },
    };
    auto override_document = json::object();
    override_document["values"]["/lod_policy"] = std::move(lod_policy);
    override_document["parts"] = json::array({
      json {
        { "kind", "surface" },
        { "id", "00000000-0000-0000-0000-000000000001" },
        { "layout_revision", std::string(63U, '0') + "1" },
        { "values", std::move(surface_values) },
      },
    });
    demo.Node("MeshNode", 0U);
    demo.Renderable(1, "/Cooked/Geometry/cube.ogeo")
      .Set("/renderables/0/overrides", std::move(override_document))
      .Append("/references/scripts", demo.Spell("/Cooked/Scripts/spin.oscript"))
      .Append(
        "/references/input_actions", demo.Spell("/Cooked/Input/jump.oiact"))
      .Append("/references/input_mapping_contexts",
        demo.Spell("/Cooked/Input/gameplay.oimap"))
      .Append("/references/physics_sidecars",
        demo.Spell("/Cooked/Scenes/PhysicsScene.opscene"));

    const auto report = Import(MakeRequest(cooked_root, demo));

    ASSERT_HAS_VALUE(report);
    EXPECT_TRUE(report->success);
    EXPECT_EQ(report->scenes_written, 1U);
    EXPECT_FALSE(HasDiagnosticCode(
      report->diagnostics, import_codes::kSceneDescriptorReferenceMissing));

    EXPECT_TRUE(HasCookedFile(cooked_root, "Scenes/DemoScene.oscene"));

    const auto scene_bytes
      = ReadBytes(CookedFile(cooked_root, "Scenes/DemoScene.oscene"));
    ASSERT_FALSE(scene_bytes.empty());

    const auto scene = oxygen::data::SceneAsset(
      oxygen::data::AssetKey {}, std::span<const std::byte>(scene_bytes));
    const auto renderables
      = scene.GetComponents<oxygen::data::pak::world::RenderableRecord>();
    ASSERT_EQ(renderables.size(), 1U);
    // Sorted by override id: material, visibility, casts shadows, LOD policy.
    using oxygen::data::schema::OverrideId;
    const auto overrides
      = scene.GetComponents<oxygen::data::pak::world::OverrideRecord>();
    ASSERT_EQ(overrides.size(), 4U);
    EXPECT_EQ(overrides[0].override_id, OverrideId::kGeometrySurfaceMaterial);
    EXPECT_EQ(overrides[0].value.as_asset,
      oxygen::data::AssetKey::FromVirtualPath("/Cooked/Materials/cube.omat"));
    EXPECT_EQ(overrides[1].override_id, OverrideId::kGeometrySurfaceVisibility);
    EXPECT_EQ(overrides[1].value.as_bool, 0U);
    EXPECT_EQ(
      overrides[2].override_id, OverrideId::kGeometrySurfaceCastsShadows);
    EXPECT_EQ(overrides[2].value.as_bool, 1U);
    EXPECT_EQ(overrides[3].override_id, OverrideId::kGeometryLodPolicy);
    EXPECT_TRUE(overrides[3].part_id.IsNil());
    const auto policies
      = scene.GetComponents<oxygen::data::pak::geometry::LodPolicyRecord>();
    ASSERT_EQ(policies.size(), 1U);
    EXPECT_EQ(overrides[3].value.as_record_index.get(), 0U);
    EXPECT_EQ(
      policies[0].type, oxygen::data::pak::geometry::LodPolicyType::kDistance);
    EXPECT_FLOAT_EQ(policies[0].thresholds[0], 8.0F);
  }

  NOLINT_TEST_F(
    SceneDescriptorImportJobTest, MissingGeometryReferenceProducesDiagnostic)
  {

    const auto cooked_root = TempDir() / "missing_geometry_reference";
    std::filesystem::create_directories(cooked_root);

    auto demo = SceneDescriptor { "/Cooked/Scenes/DemoScene.oscene" };
    demo.Node("MeshNode", 0U);
    demo.Renderable(1, "/Cooked/Geometry/missing.ogeo");

    const auto report = Import(MakeRequest(cooked_root, demo));

    ASSERT_HAS_VALUE(report);
    EXPECT_FALSE(report->success);
    EXPECT_TRUE(HasDiagnosticCode(
      report->diagnostics, import_codes::kSceneDescriptorReferenceMissing));
  }

  NOLINT_TEST_F(SceneDescriptorImportJobTest,
    DirectionalLightManualCascadeDistancesDeriveMaxShadowDistance)
  {

    const auto cooked_root = TempDir() / "directional_light_tuning";
    std::filesystem::create_directories(cooked_root);

    auto tuning = SceneDescriptor { "/Cooked/Scenes/DirectionalTuning.oscene" };
    auto sun = json::object();
    sun["node"] = 1;
    sun["common"] = json {
      { "affects_world", false },
      { "color_rgb", json::array({ 0.25, 0.5, 0.75 }) },
      { "casts_shadows", true },
      { "exposure_compensation_ev", -2 },
      {
        "shadow",
        json {
          { "bias", 0.125 },
          { "normal_bias", 0.25 },
          { "contact_shadows", true },
          { "resolution_hint", 3 },
        },
      },
    };
    sun["atmosphere_light_slot"] = 2;
    sun["use_per_pixel_atmosphere_transmittance"] = true;
    sun["atmosphere_disk_luminance_scale_rgb"] = json::array({ 0.5, 1.0, 2.0 });
    sun["angular_size_radians"] = 0.02;
    sun["cascade_count"] = 4;
    sun["cascade_distances"] = json::array({ 10.0, 30.0, 80.0, 200.0 });
    sun["distribution_exponent"] = 1.0;
    sun["intensity_lux"] = 10000.0;
    tuning.Node("Sun", 0U);
    tuning.Set("/lights/directional", json::array({ std::move(sun) }));

    const auto report = Import(MakeRequest(cooked_root, tuning));

    ASSERT_HAS_VALUE(report);
    EXPECT_TRUE(report->success);
    EXPECT_EQ(report->scenes_written, 1U);

    ASSERT_TRUE(HasCookedFile(cooked_root, "Scenes/DirectionalTuning.oscene"));

    const auto scene_bytes
      = ReadBytes(CookedFile(cooked_root, "Scenes/DirectionalTuning.oscene"));
    ASSERT_FALSE(scene_bytes.empty());

    const auto scene = oxygen::data::SceneAsset(
      oxygen::data::AssetKey {}, std::span<const std::byte>(scene_bytes));
    const auto directional
      = scene.GetComponents<oxygen::data::pak::world::DirectionalLightRecord>();
    ASSERT_EQ(directional.size(), 1U);
    EXPECT_EQ(CheckedAt(directional, 0).common.affects_world, 0U);
    EXPECT_FLOAT_EQ(CheckedAt(directional, 0).common.color_rgb[0], 0.25F);
    EXPECT_FLOAT_EQ(CheckedAt(directional, 0).common.color_rgb[1], 0.5F);
    EXPECT_FLOAT_EQ(CheckedAt(directional, 0).common.color_rgb[2], 0.75F);
    EXPECT_EQ(CheckedAt(directional, 0).common.casts_shadows, 1U);
    EXPECT_FLOAT_EQ(
      CheckedAt(directional, 0).common.exposure_compensation_ev, -2.0F);
    EXPECT_FLOAT_EQ(CheckedAt(directional, 0).common.shadow.bias, 0.125F);
    EXPECT_FLOAT_EQ(CheckedAt(directional, 0).common.shadow.normal_bias, 0.25F);
    EXPECT_EQ(CheckedAt(directional, 0).common.shadow.contact_shadows, 1U);
    EXPECT_EQ(CheckedAt(directional, 0).common.shadow.resolution_hint, 3U);
    EXPECT_EQ(CheckedAt(directional, 0).atmosphere_light_slot, 2U);
    EXPECT_EQ(
      CheckedAt(directional, 0).use_per_pixel_atmosphere_transmittance, 1U);
    EXPECT_FLOAT_EQ(
      CheckedAt(directional, 0).atmosphere_disk_luminance_scale_rgb[0], 0.5F);
    EXPECT_FLOAT_EQ(
      CheckedAt(directional, 0).atmosphere_disk_luminance_scale_rgb[1], 1.0F);
    EXPECT_FLOAT_EQ(
      CheckedAt(directional, 0).atmosphere_disk_luminance_scale_rgb[2], 2.0F);
    EXPECT_FLOAT_EQ(CheckedAt(directional, 0).angular_size_radians, 0.02F);
    EXPECT_FLOAT_EQ(CheckedAt(directional, 0).intensity_lux, 10000.0F);
    EXPECT_EQ(CheckedAt(directional, 0).split_mode, 1U);
    EXPECT_FLOAT_EQ(CheckedAt(directional, 0).max_shadow_distance, 200.0F);
    EXPECT_FLOAT_EQ(CheckedAt(directional, 0).cascade_distances[0], 10.0F);
    EXPECT_FLOAT_EQ(CheckedAt(directional, 0).cascade_distances[1], 30.0F);
    EXPECT_FLOAT_EQ(CheckedAt(directional, 0).cascade_distances[2], 80.0F);
    EXPECT_FLOAT_EQ(CheckedAt(directional, 0).cascade_distances[3], 200.0F);
    EXPECT_FLOAT_EQ(CheckedAt(directional, 0).transition_fraction, 0.1F);
    EXPECT_FLOAT_EQ(CheckedAt(directional, 0).distance_fadeout_fraction, 0.1F);

    // Exercise strict binary ingress with otherwise valid cooked content.
    for (unsigned malformed = 0U; malformed < 8U; ++malformed) {
      SCOPED_TRACE(malformed);
      auto record = CheckedAt(directional, 0);
      switch (malformed) {
      case 0:
        record.common.affects_world = 2U;
        break;
      case 1:
        record.common.shadow.contact_shadows = 2U;
        break;
      case 2:
        record.common.shadow.resolution_hint = 4U;
        break;
      case 3:
        record.atmosphere_light_slot = 3U;
        break;
      case 4:
        record.use_per_pixel_atmosphere_transmittance = 2U;
        break;
      case 5:
        record.intensity_lux = std::numeric_limits<float>::infinity();
        break;
      case 6:
        record.cascade_distances[1] = record.cascade_distances[0];
        break;
      default:
        record.atmosphere_disk_luminance_scale_rgb[1] = -1.0F;
        break;
      }
      auto malformed_bytes = scene_bytes;
      oxygen::cooker::testing::DescriptorDefects::SetComponent(malformed_bytes,
        oxygen::data::ComponentType::kDirectionalLight, 0U, record);
      EXPECT_THROW(
        static_cast<void>(oxygen::data::SceneAsset(oxygen::data::AssetKey {},
          std::span<const std::byte>(malformed_bytes))),
        std::exception);
    }
  }

  NOLINT_TEST_F(SceneDescriptorImportJobTest,
    RejectsLegacyDescriptorVersionWithRecookDiagnostic)
  {

    const auto cooked_root = TempDir() / "legacy_version";
    std::filesystem::create_directories(cooked_root);

    auto legacy = SceneDescriptor { "/Cooked/Scenes/LegacyScene.oscene" };
    legacy.Set("/schema_version", 2);

    const auto report = Import(MakeRequest(cooked_root, legacy));

    ASSERT_HAS_VALUE(report);
    EXPECT_FALSE(report->success);
    EXPECT_TRUE(HasDiagnosticCode(
      report->diagnostics, import_codes::kSceneDescriptorRecookRequired));
  }

  NOLINT_TEST_F(
    SceneDescriptorImportJobTest, SerializesV3EnvironmentAndLocalFogRecords)
  {

    const auto cooked_root = TempDir() / "environment_and_local_fog";
    std::filesystem::create_directories(cooked_root);
    auto textures = LooseCookedRoot { "Cooked" };
    const auto sky_probe = AddCubemap(textures, "Textures/sky_probe.otex");
    const auto fog_probe = AddCubemap(textures, "Textures/fog_probe.otex");
    const auto backdrop = AddCubemap(textures, "Textures/backdrop.otex");
    Materialize(textures.Files(), cooked_root);

    auto environment_scene
      = SceneDescriptor { "/Cooked/Scenes/EnvironmentScene.oscene" };
    const auto texture
      = [&environment_scene](const std::string_view leaf) -> std::string {
      return environment_scene.Spell("/Cooked/Textures/" + std::string(leaf));
    };
    auto fog_document = json {
      { "enabled", true },
      { "model", 1 },
      { "extinction_sigma_t_per_m", 0.01 },
      { "height_falloff_per_m", 0.2 },
      { "height_offset_m", 0.0 },
      { "start_distance_m", 1.0 },
      { "max_opacity", 0.8 },
      { "single_scattering_albedo_rgb", json::array({ 0.9, 0.8, 0.7 }) },
      { "anisotropy_g", 0.25 },
      { "enable_height_fog", true },
      { "enable_volumetric_fog", true },
      { "second_fog_density", 0.03 },
      { "second_fog_height_falloff", 0.15 },
      { "second_fog_height_offset", 10.0 },
      { "fog_inscattering_luminance", json::array({ 1.0, 0.9, 0.8 }) },
      {
        "sky_atmosphere_ambient_contribution_color_scale",
        json::array({ 0.2, 0.3, 0.4 }),
      },
      { "inscattering_color_cubemap_ref", texture("fog_probe.otex") },
      { "inscattering_color_cubemap_angle", 0.5 },
      { "inscattering_texture_tint", json::array({ 0.7, 0.6, 0.5 }) },
      { "fully_directional_inscattering_color_distance", 100.0 },
      { "non_directional_inscattering_color_distance", 50.0 },
      { "directional_inscattering_luminance", json::array({ 0.1, 0.2, 0.3 }) },
      { "directional_inscattering_exponent", 4.0 },
      { "directional_inscattering_start_distance", 15.0 },
      { "end_distance_m", 2500.0 },
      { "fog_cutoff_distance_m", 3000.0 },
      { "volumetric_fog_scattering_distribution", 0.6 },
      { "volumetric_fog_albedo", json::array({ 0.5, 0.6, 0.7 }) },
      { "volumetric_fog_emissive", json::array({ 0.0, 0.1, 0.2 }) },
      { "volumetric_fog_extinction_scale", 1.5 },
      { "volumetric_fog_distance", 500.0 },
      { "volumetric_fog_start_distance", 3.0 },
      { "volumetric_fog_near_fade_in_distance", 4.0 },
      { "volumetric_fog_static_lighting_scattering_intensity", 2.0 },
      { "override_light_colors_with_fog_inscattering_colors", true },
      { "holdout", false },
      { "render_in_main_pass", true },
      { "visible_in_reflection_captures", true },
      { "visible_in_real_time_sky_captures", false },
    };
    auto sky_light_document = json {
      { "enabled", true },
      { "source", 1 },
      { "cubemap_ref", texture("sky_probe.otex") },
      { "intensity", 1.5 },
      { "tint_rgb", json::array({ 0.8, 0.9, 1.0 }) },
      { "diffuse_intensity", 1.1 },
      { "specular_intensity", 1.2 },
      { "source_cubemap_angle_radians", 0.75 },
      { "lower_hemisphere_color", json::array({ 0.1, 0.2, 0.3 }) },
      { "lower_hemisphere_is_solid_color", false },
      { "lower_hemisphere_blend_alpha", 0.35 },
      { "volumetric_scattering_intensity", 0.4 },
      { "affect_reflections", true },
    };
    auto sky_sphere_document = json {
      { "enabled", true },
      { "source", 0 },
      { "cubemap_ref", texture("backdrop.otex") },
      { "solid_color_rgb", json::array({ 0.1, 0.2, 0.3 }) },
      { "intensity", 2.0 },
      { "rotation_radians", 1.25 },
      { "tint_rgb", json::array({ 0.9, 0.8, 0.7 }) },
    };
    environment_scene.Node("FogVolumeNode", 0U);
    environment_scene.Set("/environment/fog", std::move(fog_document))
      .Set("/environment/sky_light", std::move(sky_light_document))
      .Set("/environment/sky_sphere", std::move(sky_sphere_document))
      .Set("/local_fog_volumes",
        json::array({
          json {
            { "node", 1 },
            { "enabled", true },
            { "radial_fog_extinction", 0.3 },
            { "height_fog_extinction", 0.2 },
            { "height_fog_falloff", 0.15 },
            { "height_fog_offset", 1.25 },
            { "fog_phase_g", 0.4 },
            { "fog_albedo", json::array({ 0.7, 0.8, 0.9 }) },
            { "fog_emissive", json::array({ 0.1, 0.2, 0.3 }) },
            { "sort_priority", 2 },
          },
        }));

    const auto report = Import(MakeRequest(cooked_root, environment_scene));

    ASSERT_HAS_VALUE(report);
    EXPECT_TRUE(report->success);
    EXPECT_EQ(report->scenes_written, 1U);

    ASSERT_TRUE(HasCookedFile(cooked_root, "Scenes/EnvironmentScene.oscene"));

    const auto scene_bytes
      = ReadBytes(CookedFile(cooked_root, "Scenes/EnvironmentScene.oscene"));
    ASSERT_FALSE(scene_bytes.empty());

    const auto scene = oxygen::data::SceneAsset(
      oxygen::data::AssetKey {}, std::span<const std::byte>(scene_bytes));

    const auto fog = scene.TryGetFogEnvironment();
    ASSERT_HAS_VALUE(fog) << "Expected fog to contain a value";
    EXPECT_EQ(fog->enable_height_fog, 1U);
    EXPECT_EQ(fog->enable_volumetric_fog, 1U);
    EXPECT_FLOAT_EQ(fog->second_fog_density, 0.03F);
    EXPECT_FLOAT_EQ(fog->volumetric_fog_extinction_scale, 1.5F);
    EXPECT_EQ(fog->override_light_colors_with_fog_inscattering_colors, 1U);
    EXPECT_EQ(fog->visible_in_real_time_sky_captures, 0U);

    const auto sky_light = scene.TryGetSkyLightEnvironment();
    ASSERT_HAS_VALUE(sky_light) << "Expected sky_light to contain a value";
    EXPECT_FLOAT_EQ(sky_light->source_cubemap_angle_radians, 0.75F);
    EXPECT_EQ(sky_light->lower_hemisphere_is_solid_color, 0U);
    EXPECT_FLOAT_EQ(sky_light->lower_hemisphere_blend_alpha, 0.35F);
    EXPECT_FLOAT_EQ(sky_light->volumetric_scattering_intensity, 0.4F);

    const auto sky_sphere = scene.TryGetSkySphereEnvironment();
    ASSERT_HAS_VALUE(sky_sphere) << "Expected sky_sphere to contain a value";
    EXPECT_EQ(sky_sphere->source, data::pak::world::SkySphereSource::kCubemap);
    EXPECT_FLOAT_EQ(sky_sphere->intensity, 2.0F);
    EXPECT_FLOAT_EQ(sky_sphere->rotation_radians, 1.25F);
    EXPECT_FLOAT_EQ(sky_sphere->tint_rgb[2], 0.7F);
    EXPECT_FLOAT_EQ(sky_sphere->solid_color_rgb[1], 0.2F);

    // Each cubemap binds its own source texture through the reference table.
    const auto index = lc::LooseCookedIndex::LoadFromRoot(cooked_root);
    const auto scene_key = index.FindAssetKeyByVirtualPath(
      "/Cooked/Scenes/EnvironmentScene.oscene");
    ASSERT_HAS_VALUE(scene_key);
    const auto references = index.FindAssetReferences(*scene_key);
    ASSERT_HAS_VALUE(references);
    const auto& asset_references = *references;
    const auto texture_of
      = [&asset_references](
          const data::ResourceReferenceIndex reference) -> uint32_t {
      const auto resolved = asset_references.ResolveResource(
        reference, data::ResourceKind::kTexture);
      return resolved && resolved->has_value() ? resolved->value().get() : 0U;
    };
    EXPECT_EQ(texture_of(fog->inscattering_color_cubemap), fog_probe.get());
    EXPECT_EQ(texture_of(sky_light->cubemap), sky_probe.get());
    EXPECT_EQ(texture_of(sky_sphere->cubemap), backdrop.get());

    const auto local_fog
      = scene.GetComponents<oxygen::data::pak::world::LocalFogVolumeRecord>();
    ASSERT_EQ(local_fog.size(), 1U);
    EXPECT_EQ(CheckedAt(local_fog, 0).node_index, 1U);
    EXPECT_EQ(CheckedAt(local_fog, 0).enabled, 1U);
    EXPECT_FLOAT_EQ(CheckedAt(local_fog, 0).radial_fog_extinction, 0.3F);
    EXPECT_EQ(CheckedAt(local_fog, 0).sort_priority, 2);
  }

  NOLINT_TEST_F(
    SceneDescriptorImportJobTest, CompletePostProcessAndBackgroundRoundTrip)
  {

    const auto root = TempDir() / "complete_environment";
    std::filesystem::create_directories(root);
    auto document = SceneDescriptor { "/Cooked/Scenes/Environment.oscene" };
    document
      .Set("/environment/post_process_volume",
        json {
          { "tone_mapper", 1 },
          { "exposure_mode", 2 },
          { "exposure_enabled", false },
          { "exposure_compensation_ev", 1.25 },
          { "exposure_key", 8.75 },
          { "manual_exposure_ev", 11.5 },
          { "auto_exposure_min_ev", -3.25 },
          { "auto_exposure_max_ev", 12.75 },
          { "auto_exposure_speed_up", 5.5 },
          { "auto_exposure_speed_down", 1.75 },
          { "auto_exposure_metering_mode", 0 },
          { "auto_exposure_low_percentile", 0.2 },
          { "auto_exposure_high_percentile", 0.85 },
          { "auto_exposure_min_log_luminance", -10.5 },
          { "auto_exposure_log_luminance_range", 21.25 },
          { "auto_exposure_target_luminance", 0.27 },
          { "auto_exposure_spot_meter_radius", 0.35 },
          { "auto_exposure_black_influence", 0.4 },
          { "auto_exposure_transition_distance_ev", 2.5 },
          {
            "auto_exposure_compensation_curve",
            json::array({
              json { { "metered_ev", -4 }, { "compensation_ev", 1.25 } },
              json { { "metered_ev", 12 }, { "compensation_ev", -0.5 } },
            }),
          },
          { "bloom_intensity", 0.6 },
          { "bloom_threshold", 2.25 },
          { "saturation", 0.8 },
          { "contrast", 1.4 },
          { "vignette_intensity", 0.3 },
          { "display_gamma", 2.4 },
          { "enabled", true },
        })
      .Set("/environment/background",
        json {
          { "enabled", true },
          { "color_rgb", json::array({ 0.05, 0.25, 0.75 }) },
        });
    for (uint32_t tone = 0; tone != 4; ++tone) {
      for (uint32_t exposure = 0; exposure != 3; ++exposure) {
        for (uint32_t metering = 0; metering != 3; ++metering) {
          document.Set("/environment/post_process_volume/tone_mapper", tone)
            .Set("/environment/post_process_volume/exposure_mode", exposure)
            .Set("/environment/post_process_volume/auto_exposure_metering_mode",
              metering)
            .Set("/environment/post_process_volume/exposure_enabled",
              exposure == 1);
          SCOPED_TRACE(document.Text());
          auto request = MakeRequest(root, document);
          const auto report = Import(std::move(request));
          ASSERT_HAS_VALUE(report);
          ASSERT_TRUE(report->success);
          EXPECT_EQ(report->scenes_written, 1U);
          const auto bytes
            = ReadBytes(CookedFile(root, "Scenes/Environment.oscene"));
          const auto scene = data::SceneAsset(
            data::AssetKey {}, std::span<const std::byte>(bytes));
          const auto post = scene.TryGetPostProcessVolumeEnvironment();
          ASSERT_HAS_VALUE(post);
          // The record's prefix, then the two curve keys the scene holds.
          EXPECT_EQ(post->header.record_size,
            sizeof(data::pak::world::PostProcessVolumeEnvironmentRecord)
              + (2U * sizeof(data::pak::world::ExposureCompensationKeyRecord)));
          EXPECT_EQ(static_cast<uint32_t>(post->tone_mapper), tone);
          EXPECT_EQ(static_cast<uint32_t>(post->exposure_mode), exposure);
          EXPECT_EQ(
            static_cast<uint32_t>(post->auto_exposure_metering_mode), metering);
          EXPECT_EQ(post->exposure_enabled, exposure == 1 ? 1U : 0U);
          EXPECT_FLOAT_EQ(post->exposure_compensation_ev, 1.25F);
          EXPECT_FLOAT_EQ(post->exposure_key, 8.75F);
          EXPECT_FLOAT_EQ(post->manual_exposure_ev, 11.5F);
          EXPECT_FLOAT_EQ(post->auto_exposure_min_ev, -3.25F);
          EXPECT_FLOAT_EQ(post->auto_exposure_max_ev, 12.75F);
          EXPECT_FLOAT_EQ(post->auto_exposure_speed_up, 5.5F);
          EXPECT_FLOAT_EQ(post->auto_exposure_speed_down, 1.75F);
          EXPECT_FLOAT_EQ(post->auto_exposure_low_percentile, 0.2F);
          EXPECT_FLOAT_EQ(post->auto_exposure_high_percentile, 0.85F);
          EXPECT_FLOAT_EQ(post->auto_exposure_min_log_luminance, -10.5F);
          EXPECT_FLOAT_EQ(post->auto_exposure_log_luminance_range, 21.25F);
          EXPECT_FLOAT_EQ(post->auto_exposure_target_luminance, 0.27F);
          EXPECT_FLOAT_EQ(post->auto_exposure_spot_meter_radius, 0.35F);
          EXPECT_FLOAT_EQ(post->auto_exposure_black_influence, 0.4F);
          EXPECT_FLOAT_EQ(post->auto_exposure_transition_distance_ev, 2.5F);
          EXPECT_EQ(post->exposure_extension_version,
            data::pak::world::kExposureExtensionVersion);
          EXPECT_EQ(
            post->auto_exposure_metering_mask, data::kNoResourceReference);
          const auto curve = scene.GetPostProcessCompensationCurve();
          ASSERT_EQ(curve.size(), 2U);
          EXPECT_FLOAT_EQ(CheckedAt(curve, 0).metered_ev, -4.0F);
          EXPECT_FLOAT_EQ(CheckedAt(curve, 0).compensation_ev, 1.25F);
          EXPECT_FLOAT_EQ(CheckedAt(curve, 1).metered_ev, 12.0F);
          EXPECT_FLOAT_EQ(CheckedAt(curve, 1).compensation_ev, -0.5F);
          EXPECT_FLOAT_EQ(post->bloom_intensity, 0.6F);
          EXPECT_FLOAT_EQ(post->bloom_threshold, 2.25F);
          EXPECT_FLOAT_EQ(post->saturation, 0.8F);
          EXPECT_FLOAT_EQ(post->contrast, 1.4F);
          EXPECT_FLOAT_EQ(post->vignette_intensity, 0.3F);
          EXPECT_FLOAT_EQ(post->display_gamma, 2.4F);
          const auto background = scene.TryGetBackgroundEnvironment();
          if (!background.has_value()) {
            FAIL() << "Expected background to contain a value";
          }
          EXPECT_EQ(background->enabled, 1U);
          EXPECT_EQ(background->header.record_size, 24U);
          EXPECT_FLOAT_EQ(background->color_rgb[0], 0.05F);
          EXPECT_FLOAT_EQ(background->color_rgb[1], 0.25F);
          EXPECT_FLOAT_EQ(background->color_rgb[2], 0.75F);
        }
      }
    }
  }

  NOLINT_TEST_F(SceneDescriptorImportJobTest,
    RejectsPreviousSceneVersionWithRecookDiagnostic)
  {

    const auto root = TempDir() / "previous_scene_version";
    std::filesystem::create_directories(root);
    auto old_scene = SceneDescriptor { "/Cooked/Scenes/Old.oscene" };
    old_scene.Set("/schema_version", 5);
    const auto report = Import(MakeRequest(root, old_scene));
    ASSERT_HAS_VALUE(report);
    EXPECT_FALSE(report->success);
    EXPECT_EQ(report->scenes_written, 0U);
    EXPECT_TRUE(HasDiagnosticCode(
      report->diagnostics, import_codes::kSceneDescriptorRecookRequired));
  }

  //! Packs a procedural geometry with one surface and a material under
  //! /Engine, the way the engine content pak ships them.
  auto WriteEnginePak(const std::filesystem::path& work)
    -> std::filesystem::path
  {
    const auto loose = work / "engine_loose";
    auto geometry = GeometryBuilder { "Cube" };
    geometry.Submesh(geometry.ProceduralLod(),
      data::SurfaceId::FromStableIdentity("fixture-surface"));
    const auto material = MaterialBuilder { "Default" };
    {
      auto writer = LooseCookedWriter(
        loose, oxygen::content::cooked::RealCookerStorage());
      writer.WriteAssetDescriptor(data::AssetKey::FromVirtualPath(kEngineCube),
        data::AssetType::kGeometry, kEngineCube,
        "Generated/BasicShapes/Cube.ogeo", geometry.Bytes(),
        geometry.References(),
        content::import::LooseCookedProducer {
          .source = std::string(kEngineCube) + ".json",
          .completes = {},
        });
      writer.WriteAssetDescriptor(
        data::AssetKey::FromVirtualPath(kEngineDefault),
        data::AssetType::kMaterial, kEngineDefault,
        "Generated/Materials/Default.omat", material.Bytes(),
        material.References(),
        content::import::LooseCookedProducer {
          .source = std::string(kEngineDefault) + ".json",
          .completes = {},
        });
      static_cast<void>(writer.Finish());
    }
    const auto pak_path = work / "engine.pak";
    const auto built = pak::PakBuilder {}.Build(pak::PakBuildRequest {
      .mode = pak::BuildMode::kFull,
      .sources = { data::CookedSource {
        .kind = data::CookedSourceKind::kLooseCooked,
        .path = loose,
      }, },
      .output_pak_path = pak_path,
      .content_version = 1U,
      .source_key = data::SourceKey { Uuid::Generate() },
    });
    if (!built) {
      throw std::runtime_error("The engine test pak could not be built");
    }
    return pak_path;
  }

  //! A project scene resolves /Engine geometry and the material of a surface
  //! override from a pak context.
  NOLINT_TEST_F(
    SceneDescriptorImportJobTest, ResolvesEngineReferencesFromAPakContext)
  {
    // Arrange
    const auto output = TempDir() / "pak_context_output";
    std::filesystem::create_directories(output);
    const auto surface
      = data::to_string(data::SurfaceId::FromStableIdentity("fixture-surface"));
    auto scene_document = SceneDescriptor { "/Cooked/Scenes/Scene.oscene" };
    auto surface_values = json::object();
    surface_values["/material"] = scene_document.Spell(kEngineDefault);
    scene_document.Set("/nodes/0/name", "Mesh")
      .Renderable(0, kEngineCube)
      .Set("/renderables/0/overrides/parts",
        json::array({
          json {
            { "kind", "surface" },
            { "id", surface },
            { "layout_revision", std::string(63U, '0') + "1" },
            { "values", std::move(surface_values) },
          },
        }));
    auto request = MakeRequest(output, scene_document);
    request.cooked_context_roots = { WriteEnginePak(TempDir()) };

    // Act
    const auto report = Import(std::move(request));

    // Assert
    ASSERT_HAS_VALUE(report);
    EXPECT_TRUE(report->success);
    const auto scene = data::SceneAsset(
      data::AssetKey::FromVirtualPath("/Cooked/Scenes/Scene.oscene"),
      ReadBytes(CookedFile(output, "Scenes/Scene.oscene")));
    const auto renderables
      = scene.GetComponents<data::pak::world::RenderableRecord>();
    ASSERT_EQ(renderables.size(), 1U);
    EXPECT_EQ(CheckedAt(renderables, 0).geometry_key,
      data::AssetKey::FromVirtualPath(kEngineCube));
  }

  //! An override of a surface the geometry does not declare is kept in the
  //! scene, but the cook reports it as a warning and leaves it out, so the
  //! surface draws with the geometry's own defaults.
  NOLINT_TEST_F(
    SceneDescriptorImportJobTest, CooksWithoutAnOverrideOfAnUndeclaredSurface)
  {
    // Arrange
    const auto output = TempDir() / "undeclared_surface_output";
    std::filesystem::create_directories(output);
    const auto surface = data::to_string(
      data::SurfaceId::FromStableIdentity("surface-the-cube-does-not-declare"));
    auto scene_document = SceneDescriptor { "/Cooked/Scenes/Scene.oscene" };
    auto surface_values = json::object();
    surface_values["/visible"] = false;
    scene_document.Set("/nodes/0/name", "Mesh")
      .Renderable(0, kEngineCube)
      .Set("/renderables/0/overrides/parts",
        json::array({
          json {
            { "kind", "surface" },
            { "id", surface },
            { "layout_revision", std::string(63U, '0') + "1" },
            { "values", std::move(surface_values) },
          },
        }));
    auto request = MakeRequest(output, scene_document);
    request.cooked_context_roots = { WriteEnginePak(TempDir()) };

    // Act
    const auto report = Import(std::move(request));

    // Assert
    ASSERT_HAS_VALUE(report);
    EXPECT_TRUE(report->success);
    const auto warning = std::ranges::find_if(
      report->diagnostics, [](const ImportDiagnostic& diagnostic) -> bool {
        return diagnostic.code == import_codes::kSceneDescriptorSurfaceInvalid;
      });
    ASSERT_NE(warning, report->diagnostics.end());
    EXPECT_EQ(warning->severity, ImportSeverity::kWarning);
    const auto scene = data::SceneAsset(
      data::AssetKey::FromVirtualPath("/Cooked/Scenes/Scene.oscene"),
      ReadBytes(CookedFile(output, "Scenes/Scene.oscene")));
    EXPECT_EQ(
      scene.GetComponents<data::pak::world::RenderableRecord>().size(), 1U);
    EXPECT_TRUE(
      scene.GetComponents<data::pak::world::OverrideRecord>().empty());
  }

} // namespace

} // namespace oxygen::content::import::test
