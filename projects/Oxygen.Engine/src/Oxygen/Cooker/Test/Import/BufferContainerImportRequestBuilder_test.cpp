//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/BufferContainerImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/BufferContainerImportSettings.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>

namespace {

using nlohmann::json;
using oxygen::content::import::BufferContainerImportSettings;
using oxygen::content::import::EffectiveContentHashingEnabled;
using oxygen::content::import::internal::BuildBufferContainerRequest;

auto MakeTempDir(const std::string_view stem) -> std::filesystem::path
{
  auto dir = std::filesystem::temp_directory_path() / "oxygen_buffer_container";
  dir /= std::filesystem::path { std::string { stem } };
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir);
  return dir;
}

auto WriteTextFile(
  const std::filesystem::path& path, const std::string_view text) -> void
{
  std::filesystem::create_directories(path.parent_path());
  auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(out.is_open());
  out << text;
}

auto MakeBaseSettings(const std::filesystem::path& descriptor_path)
  -> BufferContainerImportSettings
{
  auto settings = BufferContainerImportSettings {};
  settings.descriptor_path = descriptor_path.string();
  settings.cooked_root = (descriptor_path.parent_path() / ".cooked").string();
  settings.job_name = "manifest-buffer-container";
  settings.with_content_hashing = true;
  return settings;
}

NOLINT_TEST(BufferContainerImportRequestBuilderTest,
  BuildsRequestFromValidDescriptorWithNormalizedPayload)
{
  const auto dir = MakeTempDir("valid_request");
  const auto descriptor_path = dir / "Buffers" / "character.buffers.json";
  WriteTextFile(descriptor_path,
    R"({
      "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.buffer-container.schema.json",
      "name": "CharacterBuffers",
      "content_hashing": false,
      "buffers": [
        {
          "source": "mesh_vertices.buffer.bin",
          "virtual_path": "/.cooked/Resources/Buffers/character_vertices.obuf",
          "usage_flags": 3,
          "element_stride": 32,
          "alignment": 16
        }
      ]
    })");

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildBufferContainerRequest(settings, errors);

  ASSERT_TRUE(request.has_value()) << errors.str();
  EXPECT_TRUE(errors.str().empty());
  ASSERT_TRUE(request->cooked_root.has_value());
  EXPECT_TRUE(request->cooked_root->is_absolute());
  EXPECT_EQ(request->source_path, descriptor_path.lexically_normal());
  EXPECT_EQ(request->job_name,
    std::optional<std::string> { "manifest-buffer-container" });
  EXPECT_FALSE(
    EffectiveContentHashingEnabled(request->options.with_content_hashing));
  ASSERT_TRUE(request->buffer_container.has_value());

  const auto normalized
    = json::parse(request->buffer_container->normalized_descriptor_json);
  EXPECT_EQ(normalized.at("name").get<std::string>(), "CharacterBuffers");
  ASSERT_TRUE(normalized.at("buffers").is_array());
  ASSERT_EQ(normalized.at("buffers").size(), 1U);
  EXPECT_EQ(normalized.at("buffers")[0].at("virtual_path").get<std::string>(),
    "/.cooked/Resources/Buffers/character_vertices.obuf");
}

NOLINT_TEST(BufferContainerImportRequestBuilderTest,
  RejectsDescriptorWithSchemaViolations)
{
  const auto dir = MakeTempDir("schema_violation");
  const auto descriptor_path = dir / "Buffers" / "bad.buffers.json";
  WriteTextFile(descriptor_path,
    R"({
      "name": "BadContainer",
      "buffers": [
        {
          "source": "mesh.buffer.bin",
          "virtual_path": "/.cooked/Resources/Buffers/bad.obuf",
          "unexpected": true
        }
      ]
    })");

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildBufferContainerRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("buffer.container.schema_validation_failed")
    != std::string::npos);
}

NOLINT_TEST(BufferContainerImportRequestBuilderTest, RejectsMissingFile)
{
  const auto dir = MakeTempDir("missing_file");
  const auto descriptor_path = dir / "Buffers" / "missing.buffers.json";
  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildBufferContainerRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("failed to open buffer-container descriptor")
    != std::string::npos);
}

NOLINT_TEST(BufferContainerImportRequestBuilderTest, RejectsRelativeCookedRoot)
{
  const auto dir = MakeTempDir("relative_output");
  const auto descriptor_path = dir / "Buffers" / "ok.buffers.json";
  WriteTextFile(descriptor_path,
    R"({
      "name": "Container",
      "buffers": [
        {
          "source": "mesh.buffer.bin",
          "virtual_path": "/.cooked/Resources/Buffers/ok.obuf",
          "element_stride": 16
        }
      ]
    })");

  auto settings = MakeBaseSettings(descriptor_path);
  settings.cooked_root = "relative/output";
  auto errors = std::ostringstream {};

  const auto request = BuildBufferContainerRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("cooked root must be an absolute path")
    != std::string::npos);
}

NOLINT_TEST(
  BufferContainerImportRequestBuilderTest, RejectsVirtualPathWithBackslashes)
{
  const auto dir = MakeTempDir("invalid_virtual_path_backslashes");
  const auto descriptor_path = dir / "Buffers" / "bad_virtual.buffers.json";
  WriteTextFile(descriptor_path,
    R"({
      "name": "Container",
      "buffers": [
        {
          "source": "mesh.buffer.bin",
          "virtual_path": "/.cooked\\Resources\\Buffers\\bad.obuf",
          "element_stride": 16
        }
      ]
    })");

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildBufferContainerRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("buffer.container.schema_validation_failed")
    != std::string::npos);
}

NOLINT_TEST(
  BufferContainerImportRequestBuilderTest, AcceptsBufferViewsWithElementRanges)
{
  const auto dir = MakeTempDir("accepts_buffer_views");
  const auto descriptor_path = dir / "Buffers" / "views.buffers.json";
  WriteTextFile(descriptor_path,
    R"({
      "name": "Container",
      "buffers": [
        {
          "source": "mesh.buffer.bin",
          "virtual_path": "/.cooked/Resources/Buffers/views.obuf",
          "element_format": 10,
          "views": [
            {
              "name": "lod0",
              "element_offset": 0,
              "element_count": 12
            }
          ]
        }
      ]
    })");

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildBufferContainerRequest(settings, errors);

  ASSERT_TRUE(request.has_value()) << errors.str();
  EXPECT_TRUE(errors.str().empty());
}

NOLINT_TEST(BufferContainerImportRequestBuilderTest,
  RejectsExplicitImplicitAllViewDeclaration)
{
  const auto dir = MakeTempDir("rejects_explicit_all_view");
  const auto descriptor_path = dir / "Buffers" / "bad_view.buffers.json";
  WriteTextFile(descriptor_path,
    R"({
      "name": "Container",
      "buffers": [
        {
          "source": "mesh.buffer.bin",
          "virtual_path": "/.cooked/Resources/Buffers/views.obuf",
          "element_stride": 16,
          "views": [
            {
              "name": "__all__",
              "byte_offset": 0,
              "byte_length": 64
            }
          ]
        }
      ]
    })");

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildBufferContainerRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("buffer.container.schema_validation_failed")
    != std::string::npos);
}

NOLINT_TEST(BufferContainerImportRequestBuilderTest,
  RejectsViewWithBothByteAndElementRanges)
{
  const auto dir = MakeTempDir("rejects_mixed_view_ranges");
  const auto descriptor_path = dir / "Buffers" / "mixed_view.buffers.json";
  WriteTextFile(descriptor_path,
    R"({
      "name": "Container",
      "buffers": [
        {
          "source": "mesh.buffer.bin",
          "virtual_path": "/.cooked/Resources/Buffers/views.obuf",
          "element_stride": 16,
          "views": [
            {
              "name": "lod0",
              "byte_offset": 0,
              "byte_length": 64,
              "element_offset": 0,
              "element_count": 4
            }
          ]
        }
      ]
    })");

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildBufferContainerRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("buffer.container.schema_validation_failed")
    != std::string::npos);
}

} // namespace
