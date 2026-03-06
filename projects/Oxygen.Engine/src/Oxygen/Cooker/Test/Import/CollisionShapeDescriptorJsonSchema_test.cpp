//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using nlohmann::json_schema::error_handler;
using nlohmann::json_schema::json_validator;

class CollectingErrorHandler final : public error_handler {
public:
  void error(const json::json_pointer& ptr, const json& instance,
    const std::string& message) override
  {
    auto out = std::ostringstream {};
    const auto path = ptr.to_string();
    out << (path.empty() ? "<root>" : path) << ": " << message;
    if (!instance.is_discarded()) {
      out << " (value=" << instance.dump() << ")";
    }
    errors_.push_back(out.str());
  }

  [[nodiscard]] auto HasErrors() const noexcept -> bool
  {
    return !errors_.empty();
  }

  [[nodiscard]] auto ToString() const -> std::string
  {
    auto out = std::ostringstream {};
    for (const auto& error : errors_) {
      out << "- " << error << "\n";
    }
    return out.str();
  }

private:
  std::vector<std::string> errors_;
};

auto FindRepoRoot() -> std::filesystem::path
{
  auto path = std::filesystem::path(__FILE__).parent_path();
  while (!path.empty()) {
    if (std::filesystem::exists(path / "src" / "Oxygen" / "Cooker" / "Import"
          / "Schemas" / "oxygen.collision-shape-descriptor.schema.json")) {
      return path;
    }
    path = path.parent_path();
  }
  return {};
}

auto SchemaFile(const std::filesystem::path& repo_root) -> std::filesystem::path
{
  return repo_root / "src" / "Oxygen" / "Cooker" / "Import" / "Schemas"
    / "oxygen.collision-shape-descriptor.schema.json";
}

auto LoadJsonFile(const std::filesystem::path& path) -> std::optional<json>
{
  auto in = std::ifstream(path);
  if (!in) {
    return std::nullopt;
  }
  try {
    auto parsed = json {};
    in >> parsed;
    return parsed;
  } catch (...) {
    return std::nullopt;
  }
}

auto ValidateSchema(
  const json& schema, const json& instance, std::string& errors) -> bool
{
  try {
    auto validator = json_validator {};
    validator.set_root_schema(schema);
    auto handler = CollectingErrorHandler {};
    [[maybe_unused]] auto _ = validator.validate(instance, handler);
    if (handler.HasErrors()) {
      errors = handler.ToString();
      return false;
    }
    return true;
  } catch (const std::exception& ex) {
    errors = ex.what();
    return false;
  }
}

//! Verifies canonical primitive collision-shape descriptors pass schema checks.
NOLINT_TEST(
  CollisionShapeDescriptorJsonSchemaTest, AcceptsCanonicalPrimitiveDocument)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.collision-shape-descriptor.schema.json",
    "name": "floor_box",
    "shape_type": "box",
    "material_ref": "/.cooked/Physics/Materials/ground.opmat",
    "half_extents": [25.0, 0.5, 25.0],
    "local_position": [0.0, -0.5, 0.0],
    "collision_own_layer": 1,
    "collision_target_layers": 18446744073709551615
  })");

  auto errors = std::string {};
  EXPECT_TRUE(ValidateSchema(*schema, doc, errors)) << errors;
}

//! Verifies canonical payload-backed collision-shape descriptors are accepted.
NOLINT_TEST(
  CollisionShapeDescriptorJsonSchemaTest, AcceptsCanonicalPayloadBackedDocument)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "name": "hull_shape",
    "shape_type": "convex_hull",
    "material_ref": "/.cooked/Physics/Materials/steel.opmat",
    "payload_ref": "/.cooked/Physics/Resources/hull.opres"
  })");

  auto errors = std::string {};
  EXPECT_TRUE(ValidateSchema(*schema, doc, errors)) << errors;
}

//! Verifies payload-backed shape types may omit payload_ref in Phase 3.
NOLINT_TEST(CollisionShapeDescriptorJsonSchemaTest,
  AcceptsPayloadBackedShapeWithoutPayloadRef)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "name": "hull_shape",
    "shape_type": "convex_hull",
    "material_ref": "/.cooked/Physics/Materials/steel.opmat"
  })");

  auto errors = std::string {};
  EXPECT_TRUE(ValidateSchema(*schema, doc, errors)) << errors;
}

//! Verifies primitive shape types reject payload references.
NOLINT_TEST(
  CollisionShapeDescriptorJsonSchemaTest, RejectsPayloadRefForPrimitiveShape)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "name": "sphere_shape",
    "shape_type": "sphere",
    "material_ref": "/.cooked/Physics/Materials/steel.opmat",
    "radius": 1.0,
    "payload_ref": "/.cooked/Physics/Resources/not_allowed.opres"
  })");

  auto errors = std::string {};
  EXPECT_FALSE(ValidateSchema(*schema, doc, errors));
}

NOLINT_TEST(
  CollisionShapeDescriptorJsonSchemaTest, AcceptsCompoundShapeWithChildren)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "name": "compound_shape",
    "shape_type": "compound",
    "material_ref": "/.cooked/Physics/Materials/steel.opmat",
    "children": [
      {
        "shape_type": "sphere",
        "radius": 0.4,
        "local_position": [0.0, 0.0, 0.0],
        "local_rotation": [0.0, 0.0, 0.0, 1.0],
        "local_scale": [1.0, 1.0, 1.0]
      },
      {
        "shape_type": "convex_hull",
        "payload_ref": "/.cooked/Physics/Resources/child_hull.opres",
        "local_position": [0.7, 0.0, 0.0],
        "local_rotation": [0.0, 0.0, 0.0, 1.0],
        "local_scale": [1.0, 1.0, 1.0]
      }
    ]
  })");

  auto errors = std::string {};
  EXPECT_TRUE(ValidateSchema(*schema, doc, errors)) << errors;
}

NOLINT_TEST(
  CollisionShapeDescriptorJsonSchemaTest, RejectsCompoundShapeWithoutChildren)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "name": "compound_shape",
    "shape_type": "compound",
    "material_ref": "/.cooked/Physics/Materials/steel.opmat",
    "payload_ref": "/.cooked/Physics/Resources/legacy_compound_payload.opres"
  })");

  auto errors = std::string {};
  EXPECT_FALSE(ValidateSchema(*schema, doc, errors));
}

} // namespace
