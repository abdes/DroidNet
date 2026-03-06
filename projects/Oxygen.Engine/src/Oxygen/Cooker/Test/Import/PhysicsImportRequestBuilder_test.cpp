//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <sstream>
#include <string>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/PhysicsImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/PhysicsImportSettings.h>

namespace {

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
  settings.target_scene_virtual_path = "/.cooked/Scenes/TestScene.oscene";
  return settings;
}

auto MakeValidInlineSettings() -> PhysicsSidecarImportSettings
{
  auto settings = MakeValidSourceSettings();
  settings.source_path.clear();
  settings.inline_bindings_json
    = R"({"bindings":{"rigid_bodies":[{"node_index":0,"shape_ref":"/.cooked/Physics/Shapes/test_shape.ocshape","material_ref":"/.cooked/Physics/Materials/default.opmat"}]}})";
  return settings;
}

NOLINT_TEST(
  PhysicsImportRequestBuilderTest, BuildPhysicsSidecarRequestValidSourceInput)
{
  const auto settings = MakeValidSourceSettings();
  auto errors = std::ostringstream {};

  const auto request = BuildPhysicsSidecarRequest(settings, errors);

  ASSERT_TRUE(request.has_value()) << errors.str();
  EXPECT_TRUE(errors.str().empty());
  ASSERT_TRUE(request->physics.has_value());
  EXPECT_EQ(request->physics->target_scene_virtual_path,
    settings.target_scene_virtual_path);
  EXPECT_TRUE(request->physics->inline_bindings_json.empty());
  ASSERT_TRUE(request->cooked_root.has_value());
  EXPECT_TRUE(request->cooked_root->is_absolute());
}

NOLINT_TEST(
  PhysicsImportRequestBuilderTest, BuildPhysicsSidecarRequestValidInlineInput)
{
  const auto settings = MakeValidInlineSettings();
  auto errors = std::ostringstream {};

  const auto request = BuildPhysicsSidecarRequest(settings, errors);

  ASSERT_TRUE(request.has_value()) << errors.str();
  EXPECT_TRUE(errors.str().empty());
  ASSERT_TRUE(request->physics.has_value());
  EXPECT_EQ(request->physics->target_scene_virtual_path,
    settings.target_scene_virtual_path);
  EXPECT_TRUE(request->physics->inline_bindings_json.find("\"bindings\"")
    != std::string::npos);
}

NOLINT_TEST(PhysicsImportRequestBuilderTest,
  BuildPhysicsSidecarRequestRejectsInlineBindingsObjectPayload)
{
  auto settings = MakeValidSourceSettings();
  settings.source_path.clear();
  settings.inline_bindings_json
    = R"({"rigid_bodies":[{"node_index":0,"shape_ref":"/.cooked/Physics/Shapes/test_shape.ocshape","material_ref":"/.cooked/Physics/Materials/default.opmat"}]})";
  auto errors = std::ostringstream {};

  const auto request = BuildPhysicsSidecarRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(
    errors.str().find("top-level 'bindings' object") != std::string::npos);
}

NOLINT_TEST(PhysicsImportRequestBuilderTest,
  BuildPhysicsSidecarRequestRejectsBothSourceAndInlineBindings)
{
  auto settings = MakeValidInlineSettings();
  settings.source_path = "physics/scene_physics_sidecar.json";
  auto errors = std::ostringstream {};

  const auto request = BuildPhysicsSidecarRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("exactly one of source_path or "
                                "inline_bindings_json must be provided")
    != std::string::npos);
}

NOLINT_TEST(PhysicsImportRequestBuilderTest,
  BuildPhysicsSidecarRequestRejectsMissingSourceAndInlineBindings)
{
  auto settings = MakeValidSourceSettings();
  settings.source_path.clear();
  settings.inline_bindings_json.clear();
  auto errors = std::ostringstream {};

  const auto request = BuildPhysicsSidecarRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("exactly one of source_path or "
                                "inline_bindings_json must be provided")
    != std::string::npos);
}

NOLINT_TEST(PhysicsImportRequestBuilderTest,
  BuildPhysicsSidecarRequestRejectsInvalidInlineBindingsJson)
{
  auto settings = MakeValidSourceSettings();
  settings.source_path.clear();
  settings.inline_bindings_json = "{ invalid }";
  auto errors = std::ostringstream {};

  const auto request = BuildPhysicsSidecarRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("inline_bindings_json is not valid JSON")
    != std::string::npos);
}

NOLINT_TEST(PhysicsImportRequestBuilderTest,
  BuildPhysicsSidecarRequestRejectsMissingTargetSceneVirtualPath)
{
  auto settings = MakeValidSourceSettings();
  settings.target_scene_virtual_path.clear();
  auto errors = std::ostringstream {};

  const auto request = BuildPhysicsSidecarRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("target_scene_virtual_path is required")
    != std::string::npos);
}

NOLINT_TEST(PhysicsImportRequestBuilderTest,
  BuildPhysicsSidecarRequestRejectsNonCanonicalVirtualPath)
{
  auto settings = MakeValidSourceSettings();
  settings.target_scene_virtual_path = "/.cooked/Scenes/./TestScene.oscene";
  auto errors = std::ostringstream {};

  const auto request = BuildPhysicsSidecarRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("canonical virtual path") != std::string::npos);
}

} // namespace
