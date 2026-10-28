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
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/ImportHarness.h>
#include <Oxygen/Cooker/test/Support/TestImages.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
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
      .revision = 4,
      .digest
      = "e14ee29097e2c2bdf66734216ee2426ba721f186f0ec69f354225af638a35b9d",
    },
    {
      .importer = Importer::kFbx,
      .revision = 4,
      .digest
      = "5678785ffcca919f74d48658d8ff84ccabb2ac47299fbb65ac5abe03cdc138bb",
    },
    {
      .importer = Importer::kTexture,
      .revision = 1,
      .digest
      = "2dce4eb519fb4564e9f0a4760aeb81c22fa4a77d86a378622344b5401391b9c0",
    },
    {
      .importer = Importer::kBufferContainer,
      .revision = 1,
      .digest
      = "93714f6e4706dedffd3c395ad9b50e3410b06faaa7cc305a3dc4c32339df10f5",
    },
    {
      .importer = Importer::kMaterialDescriptor,
      .revision = 1,
      .digest
      = "e27cf4d7b9c8eea64732e90423124b655a2389a9dd96c44477ba12606d78504e",
    },
    {
      .importer = Importer::kPhysicsMaterialDescriptor,
      .revision = 1,
      .digest
      = "1957ba2992afae7e688d46122fcfe1d8ea7bbfc42f202b573dad4660e217ceee",
    },
    {
      .importer = Importer::kCollisionShapeDescriptor,
      .revision = 1,
      .digest
      = "c423ba1663d54f16e0431ecb00f4e0912b45a273c6b33d54e0d508a92667c9da",
    },
    {
      .importer = Importer::kGeometryDescriptor,
      .revision = 2,
      .digest
      = "8835adccf95dfbe6e97dbf95c4f32d0fdd321b9f51e11069b29d062f060cfd04",
    },
    {
      .importer = Importer::kSceneDescriptor,
      .revision = 1,
      .digest
      = "4d2a76c6a09a34e8bfb9ba41b7802426393c04cc79a328a7cdb2be3e3dcc3457",
    },
    {
      .importer = Importer::kScriptAsset,
      .revision = 1,
      .digest
      = "af3bdb0d5ad5ac493c03762e025598c8718363cd1e4f8dd448d2fbd500f69985",
    },
    {
      .importer = Importer::kScriptingSidecar,
      .revision = 1,
      .digest
      = "03ea380d6ae4bcd42407c6d99ea96cb5da56855443fde1149c21d4e3f8ace721",
    },
    {
      .importer = Importer::kPhysicsSidecar,
      .revision = 1,
      .digest
      = "c835b5d47f2f69af87acd5a0fd6e35447c23a4790a1c3fc8a01def19b0a700ea",
    },
    {
      .importer = Importer::kInput,
      .revision = 1,
      .digest
      = "ed55ad546fea83382b34cab7d1d42605eb7b1e51d731d9ddd7b2e0d98a3a293e",
    },
  });

  //! The index records the cook, not what the importer made of its source.
  constexpr auto kIndexFileName = std::string_view { "container.index.bin" };

  //! The identity a model's products are named under in these scenarios.
  constexpr auto kModelSurfaceIdentity
    = std::string_view { "0196f2a0-5b4c-7d3e-8f10-2a3b4c5d6e7f" };

  //! The mount root the scripting scenarios cook under.
  [[nodiscard]] auto DefaultMountRoot() -> std::string
  {
    return ImportRequest {}.loose_cooked_layout.virtual_mount_root;
  }

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
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.asset_virtual_path
      = "/.cooked/Materials/" + std::string(name) + ".omat";
    request.material_descriptor = ImportRequest::MaterialDescriptorPayload {
      .normalized_descriptor_json = json { { "name", name } }.dump(),
    };
    return request;
  }

  [[nodiscard]] auto PhysicsMaterialRequest(const Scenario& scenario)
    -> ImportRequest
  {
    auto request = ImportRequest {};
    request.source_path = scenario.source / "ground.material.json";
    request.cooked_root = scenario.cooked;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.asset_virtual_path = "/.cooked/Physics/Materials/ground.opmat";
    request.physics_material_descriptor
      = ImportRequest::PhysicsMaterialDescriptorPayload {
          .normalized_descriptor_json = R"({
            "name": "ground",
            "static_friction": 0.95,
            "dynamic_friction": 0.70,
            "restitution": 0.05,
            "density": 1800.0,
            "virtual_path": "/.cooked/Physics/Materials/ground.opmat"
          })",
        };
    return request;
  }

  //! A scene of one node named `Root`, at `/.cooked/Scenes/DemoScene.oscene`.
  [[nodiscard]] auto DemoSceneRequest(const std::filesystem::path& cooked)
    -> ImportRequest
  {
    auto request = ImportRequest {};
    request.source_path = "inline://scene.oscene.json";
    request.job_name = "DemoScene";
    request.cooked_root = cooked;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.asset_virtual_path = "/.cooked/Scenes/DemoScene.oscene";
    request.scene_descriptor = ImportRequest::SceneDescriptorPayload {
      .normalized_descriptor_json = R"({"version":)"
        + std::to_string(internal::kSceneDescriptorVersion)
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
    request.options.scripting.source_root = cooked.parent_path();
    request.asset_virtual_path
      = DefaultMountRoot() + "/" + script.stem().string() + ".oscript";
    return request;
  }

  //! A scene of two nodes, at the default mount root, that scripts bind to.
  [[nodiscard]] auto DefaultMountScene(const std::filesystem::path& cooked)
    -> ImportRequest
  {
    auto request = ImportRequest {};
    request.source_path = "inline://scene.oscene.json";
    request.cooked_root = cooked;
    request.asset_virtual_path = DefaultMountRoot() + "/Part.oscene";
    request.scene_descriptor = ImportRequest::SceneDescriptorPayload {
      .normalized_descriptor_json = json {
        { "version", internal::kSceneDescriptorVersion },
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
    auto ExpectGoldenOutput(ImportRequest request) -> void
    {
      const auto importer = ImporterOf(request);
      ASSERT_HAS_VALUE(importer) << "No importer cooks the request";
      const auto cooked_root = request.cooked_root.value_or(CookedRoot());

      const auto report = Import(std::move(request));

      ASSERT_HAS_VALUE(report);
      ASSERT_TRUE(report->success) << "The fixture must cook";
      const auto digest = DigestOutputs(cooked_root, *report);
      const auto golden
        = std::ranges::find(kGolden, *importer, &GoldenOutput::importer);
      ASSERT_NE(golden, kGolden.end())
        << "No golden output for the " << nostd::to_string(*importer)
        << " importer";
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
      EXPECT_EQ(golden->revision, RevisionOf(importer))
        << nostd::to_string(importer);
    }
    EXPECT_EQ(kGolden.size(), kImporters.size());
  }

  NOLINT_TEST_F(ImporterRevisionTest, MaterialDescriptor)
  {
    const auto [source, cooked] = ScenarioRoots("material_descriptor");
    auto request = ImportRequest {};
    request.source_path = source / "HdrEmission.omat.json";
    request.job_name = "HdrEmission";
    request.cooked_root = cooked;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.asset_virtual_path = "/.cooked/Materials/HdrEmission.omat";
    request.material_descriptor = ImportRequest::MaterialDescriptorPayload {
      .normalized_descriptor_json = R"({
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
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.asset_virtual_path = "/.cooked/Physics/Materials/ground.opmat";
    request.physics_material_descriptor
      = ImportRequest::PhysicsMaterialDescriptorPayload {
          .normalized_descriptor_json = R"({
            "name": "ground",
            "static_friction": 0.95,
            "dynamic_friction": 0.65,
            "restitution": 0.05,
            "density": 1700.0,
            "combine_mode_friction": "max",
            "combine_mode_restitution": "average",
            "virtual_path": "/.cooked/Physics/Materials/ground.opmat"
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

  NOLINT_TEST_F(ImporterRevisionTest, Texture)
  {
    const auto [source, cooked] = ScenarioRoots("texture");
    const auto bmp = oxygen::cooker::test::MakeBmp(4U, 4U);
    WriteBytes(source / "checker.bmp", bmp);
    auto request = ImportRequest {};
    request.source_path = source / "checker.bmp";
    request.cooked_root = cooked;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.asset_virtual_path = "/.cooked/Textures/checker.otex";

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
            { "source", buffer_source.generic_string() },
            {
              "virtual_path",
              "/.cooked/Resources/Buffers/character_vertices.obuf",
            },
            { "usage_flags", 3U },
            { "element_stride", 16U },
            { "alignment", 16U },
          },
        }),
      },
    };
    auto request = ImportRequest {};
    request.source_path = "inline://buffer-container";
    request.cooked_root = cooked;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.buffer_container = ImportRequest::BufferContainerPayload {
      .normalized_descriptor_json = descriptor.dump(),
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
      { "material_ref", "/.cooked/Physics/Materials/ground.opmat" },
      { "half_extents", json::array({ 25.0F, 0.5F, 25.0F }) },
      { "virtual_path", "/.cooked/Physics/Shapes/floor_box.ocshape" },
    };
    auto request = ImportRequest {};
    request.source_path = source / "shape.shape.json";
    request.cooked_root = cooked;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.asset_virtual_path = "/.cooked/Physics/Shapes/floor_box.ocshape";
    request.collision_shape_descriptor
      = ImportRequest::CollisionShapeDescriptorPayload {
          .normalized_descriptor_json = descriptor.dump(),
        };

    ExpectGoldenOutput(std::move(request));
  }

  NOLINT_TEST_F(ImporterRevisionTest, GeometryDescriptor)
  {
    const auto scenario = ScenarioRoots("geometry_descriptor");
    const auto& [source, cooked] = scenario;
    ASSERT_NO_FATAL_FAILURE(
      CookPrerequisite(MaterialRequest(scenario, "default")));
    const auto bounds = json {
      { "min", json::array({ -0.5F, -0.5F, -0.5F }) },
      { "max", json::array({ 0.5F, 0.5F, 0.5F }) },
    };
    const auto descriptor = json {
      { "name", "ProceduralCube" },
      { "bounds", bounds },
      {
        "lods",
        json::array({
          json {
            { "name", "LOD0" },
            { "mesh_type", "procedural" },
            { "bounds", bounds },
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
                  { "material_ref", "/.cooked/Materials/default.omat" },
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
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.asset_virtual_path = "/.cooked/Geometry/ProceduralCube.ogeo";
    request.geometry_descriptor = ImportRequest::GeometryDescriptorPayload {
      .normalized_descriptor_json = descriptor.dump(),
    };

    ExpectGoldenOutput(std::move(request));
  }

  NOLINT_TEST_F(ImporterRevisionTest, SceneDescriptor)
  {
    const auto cooked = ScenarioRoots("scene_descriptor").cooked;
    auto request = DemoSceneRequest(cooked);
    request.scene_descriptor = ImportRequest::SceneDescriptorPayload {
      .normalized_descriptor_json = json {
        { "version", internal::kSceneDescriptorVersion },
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
            { "script_virtual_path", DefaultMountRoot() + "/logic.oscript" },
          },
        }),
      },
    };
    auto request = ImportRequest {};
    request.cooked_root = cooked;
    request.options.scripting.import_kind
      = ScriptingImportKind::kScriptingSidecar;
    request.options.scripting.target_scene_virtual_path
      = DefaultMountRoot() + "/Part.oscene";
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
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.physics = PhysicsImportSettings {
      .target_scene_virtual_path = "/.cooked/Scenes/DemoScene.oscene",
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
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.asset_virtual_path = "/.cooked/Input/Player.input";
    request.input.emplace();

    ExpectGoldenOutput(std::move(request));
  }

} // namespace

} // namespace oxygen::content::import::test
