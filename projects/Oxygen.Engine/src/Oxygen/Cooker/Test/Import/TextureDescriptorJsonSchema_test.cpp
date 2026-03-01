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
          / "Schemas" / "oxygen.texture-descriptor.schema.json")) {
      return path;
    }
    path = path.parent_path();
  }
  return {};
}

auto SchemaFile(const std::filesystem::path& repo_root) -> std::filesystem::path
{
  return repo_root / "src" / "Oxygen" / "Cooker" / "Import" / "Schemas"
    / "oxygen.texture-descriptor.schema.json";
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

NOLINT_TEST(TextureDescriptorJsonSchemaTest, AcceptsCanonicalDocument)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.texture-descriptor.schema.json",
    "source": "Textures/brick_albedo.png",
    "intent": "albedo",
    "decode": {
      "color_space": "srgb",
      "flip_y": false,
      "force_rgba": true
    },
    "mips": {
      "policy": "full",
      "filter": "kaiser",
      "filter_space": "linear",
      "renormalize": true
    },
    "output": {
      "format": "bc7_srgb",
      "bc7_quality": "high",
      "packing_policy": "d3d12"
    },
    "hdr": {
      "handling": "tonemap",
      "exposure_ev": 0.0,
      "bake_hdr": false
    },
    "cube": {
      "cubemap": false
    }
  })");

  auto errors = std::string {};
  EXPECT_TRUE(ValidateSchema(*schema, doc, errors)) << errors;
}

NOLINT_TEST(TextureDescriptorJsonSchemaTest, RejectsUnknownNestedFields)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "source": "Textures/brick_albedo.png",
    "decode": {
      "unknown_setting": true
    }
  })");

  auto errors = std::string {};
  EXPECT_FALSE(ValidateSchema(*schema, doc, errors));
}

NOLINT_TEST(TextureDescriptorJsonSchemaTest,
  RequiresCubeFaceSizeWhenEquirectToCubeEnabled)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "source": "Textures/env_panorama.hdr",
    "cube": {
      "equirect_to_cube": true
    }
  })");

  auto errors = std::string {};
  EXPECT_FALSE(ValidateSchema(*schema, doc, errors));
}

} // namespace
