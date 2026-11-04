//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/PhysicsImportRequestBuilder.cpp

#include <filesystem>
#include <string>
#include <vector>

#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequestCodes.h>
#include <Oxygen/Cooker/Import/PhysicsImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/PhysicsImportSettings.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::ImportDiagnostic;
namespace request_codes = oxygen::content::import::request_codes;
using oxygen::cooker::test::DiagnosticSummary;
using oxygen::cooker::test::HasDiagnostic;

using oxygen::content::import::PhysicsSidecarImportSettings;
using oxygen::content::import::internal::BuildPhysicsSidecarRequest;

auto MakeAbsoluteCookedRoot() -> std::string
{
  const auto cooked_root
    = std::filesystem::temp_directory_path() / "oxygen_physics_import_test";
  return cooked_root.string();
}

auto MakeValidSourceSettings() -> PhysicsSidecarImportSettings
{
  auto settings = PhysicsSidecarImportSettings {};
  settings.source_path = "physics/scene_physics_sidecar.json";
  settings.cooked_root = MakeAbsoluteCookedRoot();
  settings.job_name = "scene-physics-sidecar";
  settings.target_scene_virtual_path = "/Cooked/Scenes/TestScene.oscene";
  return settings;
}

auto MakeValidInlineSettings() -> PhysicsSidecarImportSettings
{
  auto settings = MakeValidSourceSettings();
  settings.source_path.clear();
  settings.inline_bindings_json
    = R"({"bindings":{"rigid_bodies":[{"node_index":0,"shape_ref":"/Cooked/Physics/Shapes/test_shape.ocshape","material_ref":"/Cooked/Physics/Materials/default.opmat"}]}})";
  return settings;
}

NOLINT_TEST(
  PhysicsImportRequestBuilderTest, BuildPhysicsSidecarRequestValidSourceInput)
{
  const auto settings = MakeValidSourceSettings();
  auto errors = std::vector<ImportDiagnostic> {};

  const auto request = BuildPhysicsSidecarRequest(settings, errors);

  ASSERT_HAS_VALUE(request) << DiagnosticSummary(errors);
  EXPECT_TRUE(errors.empty());
  ASSERT_HAS_VALUE(request->physics);
  EXPECT_EQ(request->physics->target_scene_virtual_path,
    settings.target_scene_virtual_path);
  EXPECT_TRUE(request->physics->inline_bindings_json.empty());
  ASSERT_HAS_VALUE(request->cooked_root);
  EXPECT_TRUE(request->cooked_root->is_absolute());
}

//! A sidecar cooks into the mount of the scene it targets.
NOLINT_TEST(
  PhysicsImportRequestBuilderTest, OutputMountRootIsTheTargetScenesMount)
{
  auto settings = MakeValidSourceSettings();
  settings.target_scene_virtual_path = "/Game/Scenes/TestScene.oscene";
  auto errors = std::vector<ImportDiagnostic> {};

  const auto request = BuildPhysicsSidecarRequest(settings, errors);

  ASSERT_HAS_VALUE(request) << DiagnosticSummary(errors);
  EXPECT_EQ(request->OutputIdentity(), "/Game/Scenes/TestScene.oscene");
  EXPECT_EQ(request->OutputMountRoot(), "/Game");
}

NOLINT_TEST(
  PhysicsImportRequestBuilderTest, BuildPhysicsSidecarRequestValidInlineInput)
{
  const auto settings = MakeValidInlineSettings();
  auto errors = std::vector<ImportDiagnostic> {};

  const auto request = BuildPhysicsSidecarRequest(settings, errors);

  ASSERT_HAS_VALUE(request) << DiagnosticSummary(errors);
  EXPECT_TRUE(errors.empty());
  ASSERT_HAS_VALUE(request->physics);
  EXPECT_EQ(request->physics->target_scene_virtual_path,
    settings.target_scene_virtual_path);
  EXPECT_THAT(request->physics->inline_bindings_json,
    ::testing::HasSubstr("\"bindings\""));
}

NOLINT_TEST(PhysicsImportRequestBuilderTest,
  BuildPhysicsSidecarRequestRejectsInlineBindingsObjectPayload)
{
  auto settings = MakeValidSourceSettings();
  settings.source_path.clear();
  settings.inline_bindings_json
    = R"({"rigid_bodies":[{"node_index":0,"shape_ref":"/Cooked/Physics/Shapes/test_shape.ocshape","material_ref":"/Cooked/Physics/Materials/default.opmat"}]})";
  auto errors = std::vector<ImportDiagnostic> {};

  const auto request = BuildPhysicsSidecarRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors,
    HasDiagnostic(oxygen::content::import::import_codes::
        kPhysicsSidecarSchemaValidationFailed));
}

NOLINT_TEST(PhysicsImportRequestBuilderTest,
  BuildPhysicsSidecarRequestRejectsBothSourceAndInlineBindings)
{
  auto settings = MakeValidInlineSettings();
  settings.source_path = "physics/scene_physics_sidecar.json";
  auto errors = std::vector<ImportDiagnostic> {};

  const auto request = BuildPhysicsSidecarRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors, HasDiagnostic(request_codes::kSourceConflict));
}

NOLINT_TEST(PhysicsImportRequestBuilderTest,
  BuildPhysicsSidecarRequestRejectsMissingSourceAndInlineBindings)
{
  auto settings = MakeValidSourceSettings();
  settings.source_path.clear();
  settings.inline_bindings_json.clear();
  auto errors = std::vector<ImportDiagnostic> {};

  const auto request = BuildPhysicsSidecarRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors, HasDiagnostic(request_codes::kSourceConflict));
}

NOLINT_TEST(PhysicsImportRequestBuilderTest,
  BuildPhysicsSidecarRequestRejectsInvalidInlineBindingsJson)
{
  auto settings = MakeValidSourceSettings();
  settings.source_path.clear();
  settings.inline_bindings_json = "{ invalid }";
  auto errors = std::vector<ImportDiagnostic> {};

  const auto request = BuildPhysicsSidecarRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors,
    HasDiagnostic(oxygen::content::import::import_codes::
        kPhysicsSidecarPayloadParseFailed));
}

NOLINT_TEST(PhysicsImportRequestBuilderTest,
  BuildPhysicsSidecarRequestRejectsMissingTargetSceneVirtualPath)
{
  auto settings = MakeValidSourceSettings();
  settings.target_scene_virtual_path.clear();
  auto errors = std::vector<ImportDiagnostic> {};

  const auto request = BuildPhysicsSidecarRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors, HasDiagnostic(request_codes::kTargetSceneMissing));
}

NOLINT_TEST(PhysicsImportRequestBuilderTest,
  BuildPhysicsSidecarRequestRejectsNonCanonicalVirtualPath)
{
  auto settings = MakeValidSourceSettings();
  settings.target_scene_virtual_path = "/Cooked/Scenes/./TestScene.oscene";
  auto errors = std::vector<ImportDiagnostic> {};

  const auto request = BuildPhysicsSidecarRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors, HasDiagnostic(request_codes::kTargetSceneNotCanonical));
}

} // namespace
