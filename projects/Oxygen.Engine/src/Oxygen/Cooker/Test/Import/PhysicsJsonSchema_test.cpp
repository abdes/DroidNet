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
#include <string_view>
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
          / "Schemas" / "oxygen.physics-sidecar.schema.json")) {
      return path;
    }
    path = path.parent_path();
  }
  return {};
}

auto SchemaFile(const std::filesystem::path& repo_root,
  const std::string_view file_name) -> std::filesystem::path
{
  return repo_root / "src" / "Oxygen" / "Cooker" / "Import" / "Schemas"
    / std::string(file_name);
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

NOLINT_TEST(PhysicsJsonSchemaTest, PhysicsSidecarSchemaAcceptsCanonicalDocument)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());

  const auto schema
    = LoadJsonFile(SchemaFile(repo_root, "oxygen.physics-sidecar.schema.json"));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.physics-sidecar.schema.json",
    "bindings": {
      "rigid_bodies": [
        {
          "node_index": 0,
          "shape_virtual_path": "/.cooked/PhysicsShapes/test_shape.ocshape",
          "material_virtual_path": "/.cooked/PhysicsMaterials/default.opmat",
          "body_type": "dynamic",
          "motion_quality": "discrete"
        }
      ],
      "colliders": [
        {
          "node_index": 1,
          "shape_virtual_path": "/.cooked/PhysicsShapes/test_shape.ocshape",
          "material_virtual_path": "/.cooked/PhysicsMaterials/default.opmat"
        }
      ],
      "joints": [
        {
          "node_index_a": 0,
          "node_index_b": 1,
          "constraint_resource_index": 5
        }
      ]
    }
  })");

  auto errors = std::string {};
  EXPECT_TRUE(ValidateSchema(*schema, doc, errors)) << errors;
}

NOLINT_TEST(PhysicsJsonSchemaTest, PhysicsSidecarSchemaRejectsUnknownFields)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());

  const auto schema
    = LoadJsonFile(SchemaFile(repo_root, "oxygen.physics-sidecar.schema.json"));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "bindings": {
      "rigid_bodies": [
        {
          "node_index": 0,
          "shape_virtual_path": "/.cooked/PhysicsShapes/test_shape.ocshape",
          "material_virtual_path": "/.cooked/PhysicsMaterials/default.opmat",
          "unknown_field": true
        }
      ]
    }
  })");

  auto errors = std::string {};
  EXPECT_FALSE(ValidateSchema(*schema, doc, errors));
}

NOLINT_TEST(PhysicsJsonSchemaTest,
  PhysicsSidecarSchemaRejectsMissingRequiredShapeReference)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());

  const auto schema
    = LoadJsonFile(SchemaFile(repo_root, "oxygen.physics-sidecar.schema.json"));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "bindings": {
      "rigid_bodies": [
        {
          "node_index": 0,
          "material_virtual_path": "/.cooked/PhysicsMaterials/default.opmat"
        }
      ]
    }
  })");

  auto errors = std::string {};
  EXPECT_FALSE(ValidateSchema(*schema, doc, errors));
}

} // namespace
