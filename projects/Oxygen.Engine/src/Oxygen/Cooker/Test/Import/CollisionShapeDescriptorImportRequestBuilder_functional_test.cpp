//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/CollisionShapeDescriptorImportRequestBuilder.cpp

#include <filesystem>
#include <optional>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/CollisionShapeDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/CollisionShapeDescriptorImportSettings.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Cooker/Test/Support/TestValues.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::content::import::CollisionShapeDescriptorImportSettings;
using oxygen::content::import::internal::BuildCollisionShapeDescriptorRequest;
using oxygen::cooker::test::kContentHashingDefault;
using oxygen::cooker::test::ScopedTempDir;
using oxygen::cooker::test::WriteText;

auto MakeBaseSettings(const std::filesystem::path& descriptor_path)
  -> CollisionShapeDescriptorImportSettings
{
  auto settings = CollisionShapeDescriptorImportSettings {};
  settings.descriptor_path = descriptor_path.string();
  settings.cooked_root = (descriptor_path.parent_path() / ".cooked").string();
  settings.job_name = "manifest-collision-shape";
  settings.with_content_hashing = true;
  return settings;
}

//! Verifies valid descriptors produce normalized collision-shape requests.
NOLINT_TEST(CollisionShapeDescriptorImportRequestBuilderTest,
  BuildsRequestFromValidDescriptorWithNormalizedPayload)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "valid_request";
  const auto descriptor_path = dir / "Physics" / "floor_box.shape.json";
  WriteText(descriptor_path,
    R"({
      "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.collision-shape-descriptor.schema.json",
      "name": "floor_box",
      "content_hashing": false,
      "shape_type": "box",
      "material_ref": "/.cooked/Physics/Materials/ground.opmat",
      "half_extents": [25.0, 0.5, 25.0],
      "virtual_path": "/.cooked/Physics/Shapes/floor_box.ocshape"
    })");

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildCollisionShapeDescriptorRequest(settings, errors);

  ASSERT_TRUE(request.has_value()) << errors.str();
  EXPECT_TRUE(errors.str().empty());
  ASSERT_TRUE(request->cooked_root.has_value());
  EXPECT_TRUE(request->cooked_root->is_absolute());
  EXPECT_EQ(request->source_path, descriptor_path.lexically_normal());
  EXPECT_EQ(request->job_name,
    std::optional<std::string> { "manifest-collision-shape" });
  // Release must hash authored content even when the descriptor opts out.
  EXPECT_EQ(request->options.with_content_hashing, kContentHashingDefault);
  ASSERT_TRUE(request->collision_shape_descriptor.has_value());

  const auto normalized = json::parse(
    request->collision_shape_descriptor->normalized_descriptor_json);
  EXPECT_EQ(normalized.at("name").get<std::string>(), "floor_box");
  EXPECT_EQ(normalized.at("shape_type").get<std::string>(), "box");
  EXPECT_EQ(normalized.at("virtual_path").get<std::string>(),
    "/.cooked/Physics/Shapes/floor_box.ocshape");
}

//! Verifies descriptor schema violations fail request construction.
NOLINT_TEST(CollisionShapeDescriptorImportRequestBuilderTest,
  RejectsDescriptorWithSchemaViolations)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "schema_violation";
  const auto descriptor_path = dir / "Physics" / "bad.shape.json";
  WriteText(descriptor_path,
    R"({
      "name": "bad_shape",
      "shape_type": "sphere",
      "material_ref": "/.cooked/Physics/Materials/ground.opmat",
      "radius": 1.0,
      "unexpected": true
    })");

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildCollisionShapeDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors.str(),
    ::testing::HasSubstr("physics.shape.descriptor.schema_validation_failed"));
}

//! Verifies missing descriptor files fail with a readable error.
NOLINT_TEST(
  CollisionShapeDescriptorImportRequestBuilderTest, RejectsMissingFile)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "missing_file";
  const auto descriptor_path = dir / "Physics" / "missing.shape.json";
  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildCollisionShapeDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors.str(),
    ::testing::HasSubstr("failed to open collision shape descriptor"));
}

//! Verifies relative cooked roots are rejected.
NOLINT_TEST(
  CollisionShapeDescriptorImportRequestBuilderTest, RejectsRelativeCookedRoot)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "relative_output";
  const auto descriptor_path = dir / "Physics" / "ok.shape.json";
  WriteText(descriptor_path,
    R"({
      "name": "ok_shape",
      "shape_type": "sphere",
      "material_ref": "/.cooked/Physics/Materials/ground.opmat",
      "radius": 1.0
    })");

  auto settings = MakeBaseSettings(descriptor_path);
  settings.cooked_root = "relative/output";
  auto errors = std::ostringstream {};

  const auto request = BuildCollisionShapeDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(
    errors.str(), ::testing::HasSubstr("cooked root must be an absolute path"));
}

} // namespace
