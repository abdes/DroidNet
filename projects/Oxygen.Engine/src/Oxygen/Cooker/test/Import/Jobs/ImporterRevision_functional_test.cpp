//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Importer.cpp, Data/PakFormatVersions.inc

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Importer.h>
#include <Oxygen/Cooker/Import/Internal/SceneSource.h>
#include <Oxygen/Cooker/Import/ModelImportState.h>
#include <Oxygen/Cooker/Import/PhysicsImportSettings.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/ImportHarness.h>
#include <Oxygen/Cooker/test/Support/TestImages.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  using nlohmann::json;
  using oxygen::cooker::test::ImportServiceTest;
  using oxygen::cooker::test::ReadBytes;
  using oxygen::cooker::test::WriteBytes;
  using oxygen::cooker::test::WriteText;

  //! What an importer cooks from its fixed source, at the revision it had.
  struct GoldenOutput {
    Importer importer;
    //! What the fixture exercises; none for the importer's plainest one.
    std::string_view scenario;
    uint32_t revision = 0;
    //! The SHA-256 of every file the job writes but the index, in path order.
    std::string_view digest;
  };

  //! The output of every importer, pinned to its revision.
  /*!
   When an importer's digest no longer matches, what it cooks changed: bump its
   revision in `PakFormatVersions.inc`, so that content an earlier revision
   cooked is cooked again, and record the new digest here with that revision.
  */
  constexpr auto kGolden = std::to_array<GoldenOutput>({
    {
      .importer = Importer::kGltf,
      .scenario = "",
      .revision = 11,
      .digest
      = "671db410123db8e042bc68b8fb3595374b3f78fed67a39de68af7ff98b8c642e",
    },
    {
      .importer = Importer::kGltf,
      .scenario = "textured",
      .revision = 11,
      .digest
      = "5253b65b927e11fb2e2e30058a16547bd8654f4aabc5e941e8a7019174ecbecb",
    },
    {
      .importer = Importer::kGltf,
      .scenario = "lights",
      .revision = 11,
      .digest
      = "7cff02e78035d2f8744b263dc6433c38c2c6fb80ae5908ee80eaca3c3602d277",
    },
    {
      .importer = Importer::kGltf,
      .scenario = "remap",
      .revision = 11,
      .digest
      = "96eeb62ed7b86a8b48e26423c4cd0e8a41804badc1cfa859d208b07b9dab2119",
    },
    {
      .importer = Importer::kGltf,
      .scenario = "tuned",
      .revision = 11,
      .digest
      = "a6bfddfb6c1547658301bef305b88983adab5b5bf69213734a4319c0dfabac47",
    },
    {
      .importer = Importer::kFbx,
      .scenario = "",
      .revision = 11,
      .digest
      = "1cfc6fb36ca84752b54afc5225189511e341fe194d77adbdbd965257fdcb4b91",
    },
    {
      .importer = Importer::kFbx,
      .scenario = "textured",
      .revision = 11,
      .digest
      = "de7e4a691e5e273a27cad3c534928a6d48f8333a679849e04a10951707cae182",
    },
    {
      .importer = Importer::kFbx,
      .scenario = "skinned",
      .revision = 11,
      .digest
      = "9e451eb6204f257bde0ee745b297867fda935ef5991b13a2d2d59999961302f6",
    },
    {
      .importer = Importer::kFbx,
      .scenario = "lights",
      .revision = 11,
      .digest
      = "c76a0409d22661b1b6e75ac88229771f8d3a45b2ed4d1240ff81f0e2c3a0e952",
    },
    {
      .importer = Importer::kFbx,
      .scenario = "tuned",
      .revision = 11,
      .digest
      = "24f1cc0f9b0e322c3f2047ce64f5321f4ec198976a737e1cdf72649d82fe77ab",
    },
    {
      .importer = Importer::kTexture,
      .revision = 5,
      .digest
      = "a29e0978e13f327d808eed9de1913c0584a2b4429a72191f72216b23aaa97552",
    },
    {
      .importer = Importer::kBufferContainer,
      .revision = 4,
      .digest
      = "2195e4857935ea5460f79e7b5d73e2702f864b2b00a68d269c06b660bac7ab38",
    },
    {
      .importer = Importer::kMaterialDescriptor,
      .revision = 3,
      .digest
      = "fe21e5f5abe3f5ef7c4338555b9fd078ee1bcd31ae507d63eb5a5ef56833f725",
    },
    {
      .importer = Importer::kPhysicsMaterialDescriptor,
      .revision = 3,
      .digest
      = "310bb833e3678497990d0a6601bf11af261d05b8d0cffd083a22d13bbed220fc",
    },
    {
      .importer = Importer::kCollisionShapeDescriptor,
      .revision = 3,
      .digest
      = "5efa1eb33601399ca411779671cc276839f0a5b1e1d169097cf626b933756b4b",
    },
    {
      .importer = Importer::kGeometryDescriptor,
      .revision = 7,
      .digest
      = "5d9d4826f484a3bb382f6d8b37c0387c5d32e6473880f4d6c78331495cf1b47d",
    },
    {
      .importer = Importer::kSceneDescriptor,
      .revision = 3,
      .digest
      = "10624776e6a592e2b43843c215261b76fd56dceb0974af9ae0da5d5276037931",
    },
    {
      .importer = Importer::kScriptAsset,
      .revision = 4,
      .digest
      = "353ba50bfa45e69b6790f674a896c771b200d248eac35529e8eb81c23b0cbb91",
    },
    {
      .importer = Importer::kScriptingSidecar,
      .revision = 3,
      .digest
      = "47843a7788026f9691b25e8564ec6bb8ec76464ba5fed6d32402f0f102e1c83c",
    },
    {
      .importer = Importer::kPhysicsSidecar,
      .revision = 6,
      .digest
      = "f09ba89bf8980775d4a2e4f3a4d3729696afcbc8133c2d5a12d987f540c768ac",
    },
    {
      .importer = Importer::kInput,
      .revision = 3,
      .digest
      = "f60dae91d76f241e775d7567bbf001cfb09345f3337e3d5bdea50d3826e474f3",
    },
  });

  //! The index records the cook, not what the importer made of its source.
  using oxygen::data::loose_cooked::kIndexFileName;

  //! The identity a model's products are named under in these scenarios.
  constexpr auto kModelSurfaceIdentity
    = std::string_view { "0196f2a0-5b4c-7d3e-8f10-2a3b4c5d6e7f" };

  //! The mount the scripting scenarios cook into.
  constexpr auto kCookedMount = std::string_view { "/Cooked" };

  //! A scenario's source and cooked roots, under the test's temporary root.
  struct Scenario {
    std::filesystem::path source;
    std::filesystem::path cooked;
  };

  [[nodiscard]] auto MaterialRequest(
    const Scenario& scenario, const std::string_view name) -> ImportRequest
  {
    auto request = ImportRequest {};
    request.source_path = scenario.source / (std::string(name) + ".omat.json");
    request.job_name = std::string(name);
    request.cooked_root = scenario.cooked;
    request.asset_virtual_path
      = "/Cooked/Materials/" + std::string(name) + ".omat";
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.material_descriptor = ImportRequest::MaterialDescriptorPayload {
      .descriptor_json = json { { "name", name } }.dump(),
    };
    return request;
  }

  [[nodiscard]] auto PhysicsMaterialRequest(const Scenario& scenario)
    -> ImportRequest
  {
    auto request = ImportRequest {};
    request.source_path = scenario.source / "ground.material.json";
    request.cooked_root = scenario.cooked;
    request.asset_virtual_path = "/Cooked/Physics/Materials/ground.opmat";
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.physics_material_descriptor
      = ImportRequest::PhysicsMaterialDescriptorPayload {
          .descriptor_json = R"({
            "name": "ground",
            "static_friction": 0.95,
            "dynamic_friction": 0.70,
            "restitution": 0.05,
            "density": 1800.0
          })",
        };
    return request;
  }

  //! A scene of one node named `Root`, at `/Cooked/Scenes/DemoScene.oscene`.
  [[nodiscard]] auto DemoSceneRequest(const std::filesystem::path& cooked)
    -> ImportRequest
  {
    auto request = ImportRequest {};
    request.source_path = "inline://scene.oscene.json";
    request.job_name = "DemoScene";
    request.cooked_root = cooked;
    request.asset_virtual_path = "/Cooked/Scenes/DemoScene.oscene";
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.scene_descriptor = ImportRequest::SceneDescriptorPayload {
      .descriptor_json = R"({"schema_version":)"
        + std::to_string(internal::kSceneDescriptorSchemaVersion)
        + R"(,"name":"DemoScene","nodes":[{"id":"00000000-0000-4000-8000-000000000001","name":"Root"}]})",
    };
    return request;
  }

  //! The script `file_name` of the scenario's sources, as a script asset stored
  //! in its own cooked descriptor, at the default mount root.
  [[nodiscard]] auto ScriptRequest(
    const Scenario& scenario, const std::string_view file_name) -> ImportRequest
  {
    const auto script = scenario.source / file_name;
    const auto& cooked = scenario.cooked;
    auto request = ImportRequest {};
    request.source_path = script;
    request.cooked_root = cooked;
    request.options.scripting.import_kind = ScriptingImportKind::kScriptAsset;
    request.options.scripting.compile_scripts = false;
    request.options.scripting.script_storage = ScriptStorageMode::kEmbedded;
    request.asset_virtual_path
      = std::string(kCookedMount) + "/" + script.stem().string() + ".oscript";
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    return request;
  }

  //! A scene of two nodes, at the default mount root, that scripts bind to.
  [[nodiscard]] auto DefaultMountScene(const std::filesystem::path& cooked)
    -> ImportRequest
  {
    auto request = ImportRequest {};
    request.source_path = "inline://scene.oscene.json";
    request.cooked_root = cooked;
    request.asset_virtual_path = std::string(kCookedMount) + "/Part.oscene";
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.scene_descriptor = ImportRequest::SceneDescriptorPayload {
      .descriptor_json = json {
        { "schema_version", internal::kSceneDescriptorSchemaVersion },
        { "name", "Part" },
        {
          "nodes",
          json::array({
            json {
              { "id", "00000000-0000-4000-8000-0000000000a1" },
              { "name", "part" },
            },
            json {
              { "id", "00000000-0000-4000-8000-0000000000b2" },
              { "name", "other" },
              { "parent", 0 },
            },
          }),
        },
      }.dump(),
    };
    return request;
  }

  //! The surface the generator names for its one-surface mesh.
  [[nodiscard]] auto GeneratedSurfaceId(const std::string_view generator)
    -> std::string
  {
    const auto mesh
      = data::GenerateMesh(std::string(generator) + "/surface", {});
    if (!mesh || mesh->SubMeshes().size() != 1U) {
      throw std::runtime_error("Expected a single-surface generated mesh");
    }
    return nostd::to_string(mesh->SubMeshes().front().SurfaceIdentity());
  }

  //! A model of the test assets, cooked as the model's record imports it.
  [[nodiscard]] auto ModelRequest(const std::string_view file,
    const std::filesystem::path& cooked) -> ImportRequest
  {
    auto request = ImportRequest {};
    request.source_path = oxygen::cooker::test::ModelPath(file);
    request.asset_virtual_path
      = oxygen::cooker::test::ModelVirtualPath(request.source_path);
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.cooked_root = cooked;
    request.surface_provenance = std::make_shared<const SurfaceProvenance>(
      Uuid::FromString(kModelSurfaceIdentity).value());
    request.model_import = ModelImportInputs {};
    return request;
  }

  class ImporterRevisionTest : public ImportServiceTest {
  protected:
    //! Cooks `request` and expects its output to be the golden one of the
    //! importer that cooks it.
    auto ExpectGoldenOutput(
      ImportRequest request, const std::string_view scenario = {}) -> void
    {
      const auto importer = ImporterOf(request);
      ASSERT_HAS_VALUE(importer) << "No importer cooks the request";
      const auto cooked_root = request.cooked_root.value_or(CookedRoot());

      const auto report = Import(std::move(request));

      ASSERT_HAS_VALUE(report);
      ASSERT_TRUE(report->success) << "The fixture must cook";
      const auto digest = DigestOutputs(cooked_root, *report);
      const auto golden = std::ranges::find_if(
        kGolden, [&](const GoldenOutput& output) -> bool {
          return output.importer == *importer && output.scenario == scenario;
        });
      ASSERT_NE(golden, kGolden.end())
        << "No golden output for the " << nostd::to_string(*importer)
        << " importer's '" << scenario << "' fixture";
      if (golden->revision != RevisionOf(*importer)) {
        ADD_FAILURE() << "The " << nostd::to_string(*importer)
                      << " importer is at revision " << RevisionOf(*importer)
                      << ": record its golden output with that revision, "
                      << "digest " << digest;
        return;
      }
      EXPECT_EQ(digest, golden->digest)
        << "What the " << nostd::to_string(*importer)
        << " importer cooks changed: bump its revision in "
        << "PakFormatVersions.inc, then record its golden output with that "
        << "revision, digest " << digest;
    }

    //! The source and cooked roots of `scenario`, created.
    [[nodiscard]] auto ScenarioRoots(const std::string_view scenario) const
      -> Scenario
    {
      const auto root = TempDir() / scenario;
      std::filesystem::create_directories(root / "source");
      std::filesystem::create_directories(root / "cooked");
      return { .source = root / "source", .cooked = root / "cooked" };
    }

    //! Cooks what the request under test reads, and expects it to succeed.
    auto CookPrerequisite(ImportRequest request) -> void
    {
      const auto report = Import(std::move(request));

      ASSERT_HAS_VALUE(report);
      ASSERT_TRUE(report->success) << "The prerequisite must cook";
    }

  private:
    [[nodiscard]] static auto DigestOutputs(
      const std::filesystem::path& cooked_root, const ImportReport& report)
      -> std::string
    {
      auto paths = std::vector<std::string> {};
      for (const auto& output : report.outputs) {
        if (std::filesystem::path(output.path).filename() != kIndexFileName) {
          paths.push_back(output.path);
        }
      }
      std::ranges::sort(paths);
      const auto [last, end] = std::ranges::unique(paths);
      paths.erase(last, end);

      auto hasher = base::Sha256 {};
      for (const auto& path : paths) {
        hasher.Update(
          std::as_bytes(std::span { path.data(), path.size() + 1 }));
        hasher.Update(ReadBytes(cooked_root / path));
      }
      return base::ToHexString(hasher.Finalize());
    }
  };

  //! Every importer has its golden output, at its current revision.
  NOLINT_TEST(ImporterRevisionCatalogTest, EveryImporterHasItsGoldenOutput)
  {
    for (const auto importer : kImporters) {
      const auto golden
        = std::ranges::find(kGolden, importer, &GoldenOutput::importer);
      ASSERT_NE(golden, kGolden.end()) << nostd::to_string(importer);
    }
    for (const auto& golden : kGolden) {
      EXPECT_EQ(golden.revision, RevisionOf(golden.importer))
        << nostd::to_string(golden.importer) << " '" << golden.scenario << "'";
    }
  }

  NOLINT_TEST_F(ImporterRevisionTest, MaterialDescriptor)
  {
    const auto [source, cooked] = ScenarioRoots("material_descriptor");
    auto request = ImportRequest {};
    request.source_path = source / "HdrEmission.omat.json";
    request.job_name = "HdrEmission";
    request.cooked_root = cooked;
    request.asset_virtual_path = "/Cooked/Materials/HdrEmission.omat";
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.material_descriptor = ImportRequest::MaterialDescriptorPayload {
      .descriptor_json = R"({
        "name": "HdrEmission",
        "parameters": {
          "emissive_color": [1.0, 0.25, 0.0],
          "emissive_intensity": 9.7
        }
      })",
    };

    ExpectGoldenOutput(std::move(request));
  }

  NOLINT_TEST_F(ImporterRevisionTest, PhysicsMaterialDescriptor)
  {
    const auto [source, cooked] = ScenarioRoots("physics_material_descriptor");
    auto request = ImportRequest {};
    request.source_path = source / "ground.material.json";
    request.cooked_root = cooked;
    request.asset_virtual_path = "/Cooked/Physics/Materials/ground.opmat";
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.physics_material_descriptor
      = ImportRequest::PhysicsMaterialDescriptorPayload {
          .descriptor_json = R"({
            "name": "ground",
            "static_friction": 0.95,
            "dynamic_friction": 0.65,
            "restitution": 0.05,
            "density": 1700.0,
            "combine_mode_friction": "max",
            "combine_mode_restitution": "average"
          })",
        };

    ExpectGoldenOutput(std::move(request));
  }

  NOLINT_TEST_F(ImporterRevisionTest, Gltf)
  {
    const auto cooked = ScenarioRoots("gltf").cooked;

    ExpectGoldenOutput(ModelRequest("static_scalar_triangle.gltf", cooked));
  }

  NOLINT_TEST_F(ImporterRevisionTest, Fbx)
  {
    const auto cooked = ScenarioRoots("fbx").cooked;

    ExpectGoldenOutput(ModelRequest("static_scalar_triangle.fbx", cooked));
  }

  //! The textures a model's materials bind, cooked with them.
  NOLINT_TEST_F(ImporterRevisionTest, GltfTextured)
  {
    const auto cooked = ScenarioRoots("gltf_textured").cooked;

    ExpectGoldenOutput(
      ModelRequest("static_textured_triangle.gltf", cooked), "textured");
  }

  NOLINT_TEST_F(ImporterRevisionTest, FbxTextured)
  {
    const auto cooked = ScenarioRoots("fbx_textured").cooked;

    ExpectGoldenOutput(
      ModelRequest("static_textured_triangle.fbx", cooked), "textured");
  }

  //! Joints and weights in the geometry.
  NOLINT_TEST_F(ImporterRevisionTest, FbxSkinned)
  {
    const auto cooked = ScenarioRoots("fbx_skinned").cooked;

    ExpectGoldenOutput(
      ModelRequest("skinned_character.fbx", cooked), "skinned");
  }

  //! Lights in the scene, with the units each format states.
  NOLINT_TEST_F(ImporterRevisionTest, GltfLights)
  {
    const auto cooked = ScenarioRoots("gltf_lights").cooked;

    ExpectGoldenOutput(ModelRequest("light_overrides.gltf", cooked), "lights");
  }

  NOLINT_TEST_F(ImporterRevisionTest, FbxLights)
  {
    const auto cooked = ScenarioRoots("fbx_lights").cooked;

    ExpectGoldenOutput(ModelRequest("light_overrides.fbx", cooked), "lights");
  }

  //! A remapped material binds the existing one and makes none of its own.
  NOLINT_TEST_F(ImporterRevisionTest, GltfMaterialRemap)
  {
    const auto scenario = ScenarioRoots("gltf_remap");
    CookPrerequisite(MaterialRequest(scenario, "Paint"));
    auto request = ModelRequest("static_scalar_triangle.gltf", scenario.cooked);
    request.options.material_remap["Scalar"] = "Materials/Paint.omat";

    ExpectGoldenOutput(std::move(request), "remap");
  }

  //! The settings a default import leaves alone.
  [[nodiscard]] auto TunedModelRequest(const std::string_view file,
    const std::filesystem::path& cooked) -> ImportRequest
  {
    auto request = ModelRequest(file, cooked);
    auto& options = request.options;
    options.coordinate.unit_normalization
      = UnitNormalizationPolicy::kPreserveSource;
    options.coordinate.bake_transforms_into_meshes = true;
    options.normal_policy = GeometryAttributePolicy::kAlwaysRecalculate;
    options.lod_generation.enabled = true;
    options.lod_generation.levels = 1;
    options.node_pruning = NodePruningPolicy::kDropEmptyNodes;
    auto& tuning = options.texture_tuning;
    tuning.enabled = true;
    tuning.max_size = 2;
    tuning.mip_policy = MipPolicy::kFullChain;
    tuning.color_output_format = Format::kRGBA8UNormSRGB;
    tuning.data_output_format = Format::kRGBA8UNorm;
    tuning.bc7_quality = Bc7Quality::kNone;
    return request;
  }

  NOLINT_TEST_F(ImporterRevisionTest, GltfTuned)
  {
    const auto cooked = ScenarioRoots("gltf_tuned").cooked;

    ExpectGoldenOutput(
      TunedModelRequest("static_textured_triangle.gltf", cooked), "tuned");
  }

  NOLINT_TEST_F(ImporterRevisionTest, FbxTuned)
  {
    const auto cooked = ScenarioRoots("fbx_tuned").cooked;

    ExpectGoldenOutput(
      TunedModelRequest("static_textured_triangle.fbx", cooked), "tuned");
  }

  NOLINT_TEST_F(ImporterRevisionTest, Texture)
  {
    const auto [source, cooked] = ScenarioRoots("texture");
    const auto bmp = oxygen::cooker::test::MakeBmp(4U, 4U);
    WriteBytes(source / "checker.bmp", bmp);
    auto request = ImportRequest {};
    request.source_path = source / "checker.bmp";
    request.cooked_root = cooked;
    request.asset_virtual_path = "/Cooked/Textures/checker.otex";
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);

    ExpectGoldenOutput(std::move(request));
  }

  NOLINT_TEST_F(ImporterRevisionTest, BufferContainer)
  {
    const auto [source, cooked] = ScenarioRoots("buffer_container");
    auto bytes = std::vector<std::byte>(32U);
    for (auto i = size_t { 0U }; i < bytes.size(); ++i) {
      bytes[i] = static_cast<std::byte>(i);
    }
    const auto buffer_source = source / "character_vertices.buffer.bin";
    WriteBytes(buffer_source, bytes);
    const auto descriptor = json {
      { "name", "CharacterBuffers" },
      {
        "buffers",
        json::array({
          json {
            {
              "source",
              buffer_source.filename().generic_string(),
            },
            {
              "virtual_path",
              "Buffers/character_vertices.obuf",
            },
            { "usage_flags", 3U },
            { "element_stride", 16U },
            { "alignment", 16U },
          },
        }),
      },
    };
    auto request = ImportRequest {};
    request.source_path = source / "inline.buffers.json";
    request.asset_virtual_path = "/Cooked/Resources/inline.buffers";
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.cooked_root = cooked;
    request.source_mounts = oxygen::cooker::test::ContentMountTable(source);
    request.buffer_container = ImportRequest::BufferContainerPayload {
      .descriptor_json = descriptor.dump(),
    };

    ExpectGoldenOutput(std::move(request));
  }

  NOLINT_TEST_F(ImporterRevisionTest, CollisionShapeDescriptor)
  {
    const auto scenario = ScenarioRoots("collision_shape_descriptor");
    const auto& [source, cooked] = scenario;
    ASSERT_NO_FATAL_FAILURE(CookPrerequisite(PhysicsMaterialRequest(scenario)));
    const auto descriptor = json {
      { "name", "floor_box" },
      { "shape_type", "box" },
      { "material_ref", "../Materials/ground.opmat" },
      { "half_extents", json::array({ 25.0F, 0.5F, 25.0F }) },
    };
    auto request = ImportRequest {};
    request.source_path = source / "shape.shape.json";
    request.cooked_root = cooked;
    request.asset_virtual_path = "/Cooked/Physics/Shapes/floor_box.ocshape";
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.collision_shape_descriptor
      = ImportRequest::CollisionShapeDescriptorPayload {
          .descriptor_json = descriptor.dump(),
        };

    ExpectGoldenOutput(std::move(request));
  }

  NOLINT_TEST_F(ImporterRevisionTest, GeometryDescriptor)
  {
    const auto scenario = ScenarioRoots("geometry_descriptor");
    const auto& [source, cooked] = scenario;
    ASSERT_NO_FATAL_FAILURE(
      CookPrerequisite(MaterialRequest(scenario, "default")));
    const auto descriptor = json {
      { "name", "ProceduralCube" },
      {
        "lods",
        json::array({
          json {
            { "name", "LOD0" },
            { "mesh_type", "procedural" },
            {
              "procedural",
              {
                { "generator", "Cube" },
                { "mesh_name", "UnitCube" },
              },
            },
            {
              "submeshes",
              json::array({
                json {
                  { "surface_id", GeneratedSurfaceId("Cube") },
                  { "material_ref", "../Materials/default.omat" },
                  {
                    "views",
                    json::array({ json { { "view_ref", "__all__" } } }),
                  },
                },
              }),
            },
          },
        }),
      },
    };
    auto request = ImportRequest {};
    request.source_path = source / "cube.procedural.geometry.json";
    request.cooked_root = cooked;
    request.asset_virtual_path = "/Cooked/Geometry/ProceduralCube.ogeo";
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.source_mounts = oxygen::cooker::test::ContentMountTable(source);
    request.geometry_descriptor = ImportRequest::GeometryDescriptorPayload {
      .descriptor_json = descriptor.dump(),
    };

    ExpectGoldenOutput(std::move(request));
  }

  NOLINT_TEST_F(ImporterRevisionTest, SceneDescriptor)
  {
    const auto cooked = ScenarioRoots("scene_descriptor").cooked;
    auto request = DemoSceneRequest(cooked);
    request.scene_descriptor = ImportRequest::SceneDescriptorPayload {
      .descriptor_json = json {
        { "schema_version", internal::kSceneDescriptorSchemaVersion },
        { "name", "DemoScene" },
        { "nodes", json::array() },
        {
          "environment",
          {
            { "background", { { "color_rgb", json::array({ 0.1, 0.2, 0.3 }) } } },
            { "post_process_volume", { { "manual_exposure_ev", 9.5 } } },
          },
        },
      }.dump(),
    };

    ExpectGoldenOutput(std::move(request));
  }

  NOLINT_TEST_F(ImporterRevisionTest, ScriptAsset)
  {
    const auto scenario = ScenarioRoots("script_asset");
    WriteText(scenario.source / "hello.luau", "return 7");
    ExpectGoldenOutput(ScriptRequest(scenario, "hello.luau"));
  }

  NOLINT_TEST_F(ImporterRevisionTest, ScriptingSidecar)
  {
    const auto scenario = ScenarioRoots("scripting_sidecar");
    const auto& [source, cooked] = scenario;
    ASSERT_NO_FATAL_FAILURE(CookPrerequisite(DefaultMountScene(cooked)));
    WriteText(source / "logic.luau",
      "local Script = oxygen.script {\n"
      "  params = { speed = oxygen.param.float {} },\n"
      "}\n"
      "return Script\n");
    ASSERT_NO_FATAL_FAILURE(
      CookPrerequisite(ScriptRequest(scenario, "logic.luau")));
    const auto bindings = json {
      {
        "bindings",
        json::array({
          json {
            { "node_index", 0 },
            { "slot_id", "00000000-0000-4000-8000-00000000000a" },
            {
              "script_virtual_path",
              "logic.oscript",
            },
          },
        }),
      },
    };
    auto request = ImportRequest {};
    request.cooked_root = cooked;
    request.options.scripting.import_kind
      = ScriptingImportKind::kScriptingSidecar;
    request.options.scripting.target_scene_virtual_path
      = std::string(kCookedMount) + "/Part.oscene";
    request.options.scripting.inline_bindings_json = bindings.dump();

    ExpectGoldenOutput(std::move(request));
  }

  NOLINT_TEST_F(ImporterRevisionTest, PhysicsSidecar)
  {
    const auto cooked = ScenarioRoots("physics_sidecar").cooked;
    ASSERT_NO_FATAL_FAILURE(CookPrerequisite(DemoSceneRequest(cooked)));
    auto request = ImportRequest {};
    request.source_path = "inline://physics-sidecar";
    request.job_name = "DemoScene.physics";
    request.cooked_root = cooked;
    request.physics = PhysicsImportSettings {
      .target_scene_virtual_path = "/Cooked/Scenes/DemoScene.oscene",
      .inline_bindings_json = R"({"bindings":{}})",
    };

    ExpectGoldenOutput(std::move(request));
  }

  NOLINT_TEST_F(ImporterRevisionTest, Input)
  {
    const auto [source, cooked] = ScenarioRoots("input");
    WriteText(source / "Player.input.json", R"({
      "actions": [{ "name": "Jump", "type": "bool" }],
      "contexts": [
        {
          "name": "Gameplay",
          "priority": 100,
          "mappings": [
            { "action": "Jump", "slot": "Space", "trigger": "pressed" }
          ]
        }
      ]
    })");
    auto request = ImportRequest {};
    request.source_path = source / "Player.input.json";
    request.cooked_root = cooked;
    request.asset_virtual_path = "/Cooked/Input/Player.input";
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.input.emplace();

    ExpectGoldenOutput(std::move(request));
  }

} // namespace

} // namespace oxygen::content::import::test
