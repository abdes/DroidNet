//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
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
          / "Schemas" / "oxygen.physics-resource-descriptor.schema.json")) {
      return path;
    }
    path = path.parent_path();
  }
  return {};
}

auto SchemaFile(const std::filesystem::path& repo_root) -> std::filesystem::path
{
  return repo_root / "src" / "Oxygen" / "Cooker" / "Import" / "Schemas"
    / "oxygen.physics-resource-descriptor.schema.json";
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

NOLINT_TEST(PhysicsResourceDescriptorJsonSchemaTest, AcceptsCanonicalDocument)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.physics-resource-descriptor.schema.json",
    "name": "park_hinge_joint_a",
    "source": "Examples/Content/physics/bin/park_hinge_joint_a.jphbin",
    "format": "jolt_constraint_binary",
    "virtual_path": "/.cooked/Physics/Resources/park_hinge_joint_a.opres"
  })");

  auto errors = std::string {};
  EXPECT_TRUE(ValidateSchema(*schema, doc, errors)) << errors;
}

NOLINT_TEST(PhysicsResourceDescriptorJsonSchemaTest, AcceptsPhysXSoftBodyFormat)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "name": "park_soft_body_settings_physx_a",
    "source": "Examples/Content/physics/bin/park_soft_body_settings_physx_a.bin",
    "format": "physx_soft_body_settings_binary",
    "virtual_path": "/.cooked/Physics/Resources/park_soft_body_settings_physx_a.opres"
  })");

  auto errors = std::string {};
  EXPECT_TRUE(ValidateSchema(*schema, doc, errors)) << errors;
}

NOLINT_TEST(
  PhysicsResourceDescriptorJsonSchemaTest, AcceptsAdditionalPhysXFormats)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  constexpr auto kFormats = std::array {
    "physx_convex_mesh_binary",
    "physx_triangle_mesh_binary",
    "physx_height_field_binary",
    "physx_constraint_binary",
    "physx_vehicle_settings_binary",
  };

  for (const auto* format : kFormats) {
    const auto doc = json::parse(std::string(R"({
      "name": "physx_variant",
      "source": "Examples/Content/physics/bin/physx_variant.bin",
      "format": ")")
      + format + R"(",
      "virtual_path": "/.cooked/Physics/Resources/physx_variant.opres"
    })");

    auto errors = std::string {};
    EXPECT_TRUE(ValidateSchema(*schema, doc, errors))
      << "format=" << format << "\n"
      << errors;
  }
}

NOLINT_TEST(PhysicsResourceDescriptorJsonSchemaTest, RejectsUnknownFields)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "source": "payload.bin",
    "format": "jolt_shape_binary",
    "unexpected": true
  })");

  auto errors = std::string {};
  EXPECT_FALSE(ValidateSchema(*schema, doc, errors));
}

NOLINT_TEST(PhysicsResourceDescriptorJsonSchemaTest, RequiresSourceAndFormat)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "name": "joint_blob"
  })");

  auto errors = std::string {};
  EXPECT_FALSE(ValidateSchema(*schema, doc, errors));
}

} // namespace
