//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <string>

#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/Internal/StaticScalarSourceValidation.h>
#include <Oxygen/Cooker/Import/Internal/gltf/cgltf.h>
#include <Oxygen/Testing/GTest.h>

#include "AsyncImporterFullTestBase.h"

namespace {

using oxygen::content::import::ImportDiagnostic;
using oxygen::content::import::ImportRequest;
using oxygen::content::import::SceneContentPolicy;
using oxygen::content::import::internal::ValidateStaticScalarSource;

class StaticScalarImportTest
  : public oxygen::content::import::test::AsyncImporterFullTestBase { };

NOLINT_TEST_F(StaticScalarImportTest, NativeImportAcceptsStaticGltfAndFbx)
{
  for (const auto* extension : { "gltf", "fbx" }) {
    SCOPED_TRACE(extension);
    const auto source = TestModelsDirFromFile()
      / (std::string("static_scalar_triangle.") + extension);
    ASSERT_TRUE(std::filesystem::exists(source));
    ImportRequest request {};
    request.source_path = source;
    request.cooked_root
      = MakeTempDir(std::string("static_scalar_") + extension);
    request.options.scene_content_policy = SceneContentPolicy::kStaticScalar;
    request.options.coordinate.bake_transforms_into_meshes = false;
    const auto result = RunImport(std::move(request));
    for (const auto& diagnostic : result.report.diagnostics) {
      EXPECT_NE(
        diagnostic.severity, oxygen::content::import::ImportSeverity::kError)
        << diagnostic.code << ": " << diagnostic.message;
    }
    ASSERT_TRUE(result.report.success);
    EXPECT_EQ(result.report.geometry_written, 1U);
    EXPECT_EQ(result.report.scenes_written, 1U);
    EXPECT_FALSE(LoadSceneReadback(result.report).renderables.empty());
  }
}

NOLINT_TEST_F(StaticScalarImportTest,
  NativeImportPreservesPerspectiveCameraAndDirectionalLight)
{
  for (const auto* extension : { "gltf", "fbx" }) {
    SCOPED_TRACE(extension);
    ImportRequest request {};
    request.source_path = TestModelsDirFromFile()
      / (std::string("static_scalar_camera_sun.") + extension);
    ASSERT_TRUE(std::filesystem::exists(request.source_path));
    request.cooked_root
      = MakeTempDir(std::string("static_scalar_camera_sun_") + extension);
    request.options.scene_content_policy = SceneContentPolicy::kStaticScalar;
    request.options.coordinate.bake_transforms_into_meshes = false;
    const auto result = RunImport(std::move(request));
    ASSERT_TRUE(result.report.success);
    const auto scene = LoadSceneReadback(result.report);
    EXPECT_EQ(scene.directional_lights.size(), 1U);
    EXPECT_TRUE(std::ranges::any_of(
      scene.component_entries, [](const auto& entry) -> bool {
        return static_cast<oxygen::data::ComponentType>(entry.component_type)
          == oxygen::data::ComponentType::kPerspectiveCamera
          && entry.table.count == 1U;
      }));
  }
}

NOLINT_TEST_F(StaticScalarImportTest,
  NativeImportRejectsUnsupportedComponentsBeforeEmission)
{
  for (const auto* extension : { "gltf", "fbx" }) {
    SCOPED_TRACE(extension);
    const auto source
      = TestModelsDirFromFile() / (std::string("light_overrides.") + extension);
    ASSERT_TRUE(std::filesystem::exists(source));
    ImportRequest request {};
    request.source_path = source;
    request.cooked_root
      = MakeTempDir(std::string("static_scalar_reject_") + extension);
    request.options.scene_content_policy = SceneContentPolicy::kStaticScalar;
    const auto result = RunImport(std::move(request));
    EXPECT_FALSE(result.report.success);
    EXPECT_EQ(result.report.geometry_written, 0U);
    EXPECT_EQ(result.report.materials_written, 0U);
    EXPECT_EQ(result.report.scenes_written, 0U);
    EXPECT_TRUE(std::ranges::any_of(
      result.report.diagnostics, [](const auto& diagnostic) -> bool {
        return diagnostic.code == "import.static_scalar.unsupported"
          && diagnostic.message.find("light components") != std::string::npos;
      }));
  }
}

NOLINT_TEST_F(StaticScalarImportTest, FbxWithoutAuthoredUnitsIsRejected)
{
  const auto root = MakeTempDir("static_scalar_missing_units");
  std::ifstream input(TestModelsDirFromFile() / "static_scalar_triangle.fbx");
  ASSERT_TRUE(input);
  const auto source = root / "ambiguous.fbx";
  {
    std::ofstream output(source);
    for (std::string line; std::getline(input, line);) {
      if (!line.contains("UnitScaleFactor")) {
        output << line << '\n';
      }
    }
  }
  ImportRequest request {};
  request.source_path = source;
  request.cooked_root = root / "cooked";
  request.options.scene_content_policy = SceneContentPolicy::kStaticScalar;
  const auto result = RunImport(std::move(request));
  EXPECT_FALSE(result.report.success);
  EXPECT_EQ(result.report.geometry_written, 0U);
  EXPECT_TRUE(std::ranges::any_of(
    result.report.diagnostics, [](const auto& diagnostic) -> bool {
      return diagnostic.code == "import.static_scalar.coordinate_metadata";
    }));
}

NOLINT_TEST(
  StaticScalarSourceValidationTest, ReportsAllUnsupportedSourceFeatures)
{
  cgltf_primitive primitive {};
  primitive.type = cgltf_primitive_type_lines;
  primitive.targets_count = 1U;
  cgltf_mesh mesh {};
  mesh.primitives = &primitive;
  mesh.primitives_count = 1U;
  cgltf_data source {};
  source.meshes = &mesh;
  source.meshes_count = 1U;
  source.animations_count = 1U;
  source.skins_count = 1U;
  source.textures_count = 1U;
  cgltf_camera camera {};
  camera.type = cgltf_camera_type_orthographic;
  cgltf_light light {};
  light.type = cgltf_light_type_point;
  source.cameras = &camera;
  source.cameras_count = 1U;
  source.lights = &light;
  source.lights_count = 1U;
  std::vector<ImportDiagnostic> diagnostics;

  EXPECT_FALSE(
    ValidateStaticScalarSource(source, "animated.gltf", diagnostics));
  EXPECT_EQ(diagnostics.size(), 7U);
  for (const auto& diagnostic : diagnostics) {
    EXPECT_EQ(diagnostic.source_path, "animated.gltf");
    EXPECT_FALSE(diagnostic.object_path.empty());
  }
}

NOLINT_TEST(StaticScalarSourceValidationTest, RejectsRequiredUnknownExtensions)
{
  std::string extension = "EXT_physics";
  std::array extensions { extension.data() };
  cgltf_data source {};
  source.extensions_required = extensions.data();
  source.extensions_required_count = 1U;
  std::vector<ImportDiagnostic> diagnostics;
  EXPECT_FALSE(ValidateStaticScalarSource(source, "physics.gltf", diagnostics));
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_NE(diagnostics.front().message.find(extension), std::string::npos);
}

NOLINT_TEST(StaticScalarSourceValidationTest, AcceptsMappedScalarExtensions)
{
  cgltf_material material {};
  material.has_clearcoat = 1;
  material.has_volume = 1;
  material.has_transmission = 1;
  cgltf_data source {};
  source.materials = &material;
  source.materials_count = 1U;
  std::vector<ImportDiagnostic> diagnostics;
  EXPECT_TRUE(ValidateStaticScalarSource(source, "scalar.gltf", diagnostics));
  EXPECT_TRUE(diagnostics.empty());
}

NOLINT_TEST(StaticScalarSourceValidationTest, RejectsUnmappedScalarFields)
{
  cgltf_material material {};
  material.has_specular = 1;
  material.specular.specular_color_factor[0] = 1.0F;
  material.has_sheen = 1;
  material.sheen.sheen_roughness_factor = 1.0F;
  cgltf_data source {};
  source.materials = &material;
  source.materials_count = 1U;
  std::vector<ImportDiagnostic> diagnostics;
  EXPECT_FALSE(ValidateStaticScalarSource(source, "scalar.gltf", diagnostics));
  EXPECT_EQ(diagnostics.size(), 2U);
}

NOLINT_TEST_F(
  StaticScalarImportTest, ManifestSelectsPolicyAndRejectsUnknownValues)
{
  const auto root = MakeTempDir("static_scalar_manifest");
  const auto path = root / "import.json";
  for (const auto* policy : { "default", "static-scalar", "invented" }) {
    SCOPED_TRACE(policy);
    {
      std::ofstream output(path);
      output
        << "{\"version\":1,\"output\":\"cooked\",\"jobs\":[{\"id\":\"model\","
           "\"type\":\"gltf\",\"source\":\"model.gltf\",\"content_policy\":\""
        << policy << "\"}]}";
    }
    std::ostringstream errors;
    const auto manifest
      = oxygen::content::import::ImportManifest::Load(path, root, errors);
    if (std::string_view(policy) == "invented") {
      EXPECT_FALSE(manifest.has_value());
      continue;
    }
    if (!manifest.has_value()) {
      ADD_FAILURE() << errors.str();
      continue;
    }
    const auto requests = manifest->BuildRequests(errors);
    ASSERT_EQ(requests.size(), 1U) << errors.str();
    EXPECT_EQ(requests.front().options.scene_content_policy,
      std::string_view(policy) == "static-scalar"
        ? SceneContentPolicy::kStaticScalar
        : SceneContentPolicy::kDefault);
  }
}

} // namespace
