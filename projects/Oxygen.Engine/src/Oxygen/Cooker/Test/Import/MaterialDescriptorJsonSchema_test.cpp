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
#include <nlohmann/json_fwd.hpp>

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
          / "Schemas" / "oxygen.material-descriptor.schema.json")) {
      return path;
    }
    path = path.parent_path();
  }
  return {};
}

auto SchemaFile(const std::filesystem::path& repo_root) -> std::filesystem::path
{
  return repo_root / "src" / "Oxygen" / "Cooker" / "Import" / "Schemas"
    / "oxygen.material-descriptor.schema.json";
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

NOLINT_TEST(MaterialDescriptorJsonSchemaTest, AcceptsCanonicalDocument)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  if (!schema.has_value()) {
    FAIL() << "Expected schema to contain a value";
  }

  const auto doc = json::parse(R"({
    "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.material-descriptor.schema.json",
    "name": "WoodFloor",
    "content_hashing": true,
    "domain": "opaque",
    "alpha_mode": "opaque",
    "orm_policy": "auto",
    "parameters": {
      "base_color": [1.0, 1.0, 1.0, 1.0],
      "metalness": 0.1,
      "roughness": 0.8
    },
    "textures": {
      "base_color": {
        "virtual_path": "/.cooked/Textures/WoodFloor_Color.otex",
        "uv_set": 0,
        "uv_transform": {
          "scale": [1.0, 1.0],
          "offset": [0.0, 0.0],
          "rotation_radians": 0.0
        }
      },
      "roughness": {
        "virtual_path": "/.cooked/Textures/WoodFloor_Roughness.otex"
      }
    },
    "shaders": [
      {
        "stage": "vertex",
        "source_path": "Vortex/Stages/Translucency/ForwardMesh_VS.hlsl",
        "entry_point": "MainVS"
      },
      {
        "stage": "pixel",
        "source_path": "Vortex/Stages/Translucency/ForwardMesh_PS.hlsl",
        "entry_point": "MainPS",
        "defines": "USE_FOG=1"
      }
    ]
  })");

  auto errors = std::string {};
  EXPECT_TRUE(ValidateSchema(*schema, doc, errors)) << errors;
}

NOLINT_TEST(MaterialDescriptorJsonSchemaTest, RejectsUnknownNestedFields)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  if (!schema.has_value()) {
    FAIL() << "Expected schema to contain a value";
  }

  const auto doc = json::parse(R"({
    "name": "WoodFloor",
    "textures": {
      "base_color": {
        "virtual_path": "/.cooked/Textures/WoodFloor_Color.otex",
        "unknown_setting": true
      }
    }
  })");

  auto errors = std::string {};
  EXPECT_FALSE(ValidateSchema(*schema, doc, errors));
}

NOLINT_TEST(MaterialDescriptorJsonSchemaTest, ValidatesCanonicalEmission)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  if (!schema.has_value()) {
    FAIL() << "Expected schema to contain a value";
  }
  auto doc = json::parse(R"({
    "name": "HDR",
    "parameters": {
      "emissive_color": [1.0, 0.25, 0.0],
      "emissive_intensity": 65504.0
    }
  })");
  auto errors = std::string {};
  EXPECT_TRUE(ValidateSchema(*schema, doc, errors)) << errors;

  doc.at("parameters").update({ { "emissive_intensity", 65505.0 } });
  EXPECT_FALSE(ValidateSchema(*schema, doc, errors));
  doc.at("parameters").update({ { "emissive_intensity", -1.0 } });
  EXPECT_FALSE(ValidateSchema(*schema, doc, errors));
  doc.at("parameters").update({ { "emissive_intensity", 0.0 } });
  doc.at("parameters").update({ { "emissive_color", { 1.1, 0.0, 0.0 } } });
  EXPECT_FALSE(ValidateSchema(*schema, doc, errors));
  doc.update(
    { { "parameters", { { "emissive_factor", { 1.0, 1.0, 1.0 } } } } });
  EXPECT_FALSE(ValidateSchema(*schema, doc, errors));
}

NOLINT_TEST(MaterialDescriptorJsonSchemaTest, RequiresTextureVirtualPath)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  if (!schema.has_value()) {
    FAIL() << "Expected schema to contain a value";
  }

  const auto doc = json::parse(R"({
    "name": "WoodFloor",
    "textures": {
      "base_color": {
        "uv_set": 0
      }
    }
  })");

  auto errors = std::string {};
  EXPECT_FALSE(ValidateSchema(*schema, doc, errors));
}

} // namespace
