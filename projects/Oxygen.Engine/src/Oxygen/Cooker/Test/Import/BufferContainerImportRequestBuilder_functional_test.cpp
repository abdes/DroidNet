//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/BufferContainerImportRequestBuilder.cpp

#include <filesystem>
#include <optional>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/BufferContainerImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/BufferContainerImportSettings.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Cooker/Test/Support/TestValues.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::content::import::BufferContainerImportSettings;
using oxygen::content::import::EffectiveContentHashingEnabled;
using oxygen::content::import::internal::BuildBufferContainerRequest;
using oxygen::cooker::test::kContentHashingDefault;
using oxygen::cooker::test::ScopedTempDir;
using oxygen::cooker::test::WriteText;

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
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "valid_request";
  const auto descriptor_path = dir / "Buffers" / "character.buffers.json";
  WriteText(descriptor_path,
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
  // Release must hash authored content even when the descriptor opts out.
  EXPECT_EQ(request->options.with_content_hashing, kContentHashingDefault);
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
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "schema_violation";
  const auto descriptor_path = dir / "Buffers" / "bad.buffers.json";
  WriteText(descriptor_path,
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
  EXPECT_THAT(errors.str(),
    ::testing::HasSubstr("buffer.container.schema_validation_failed"));
}

NOLINT_TEST(BufferContainerImportRequestBuilderTest, RejectsMissingFile)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "missing_file";
  const auto descriptor_path = dir / "Buffers" / "missing.buffers.json";
  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildBufferContainerRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors.str(),
    ::testing::HasSubstr("failed to open buffer-container descriptor"));
}

NOLINT_TEST(BufferContainerImportRequestBuilderTest, RejectsRelativeCookedRoot)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "relative_output";
  const auto descriptor_path = dir / "Buffers" / "ok.buffers.json";
  WriteText(descriptor_path,
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
  EXPECT_THAT(
    errors.str(), ::testing::HasSubstr("cooked root must be an absolute path"));
}

NOLINT_TEST(
  BufferContainerImportRequestBuilderTest, RejectsVirtualPathWithBackslashes)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "invalid_virtual_path_backslashes";
  const auto descriptor_path = dir / "Buffers" / "bad_virtual.buffers.json";
  WriteText(descriptor_path,
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
  EXPECT_THAT(errors.str(),
    ::testing::HasSubstr("buffer.container.schema_validation_failed"));
}

NOLINT_TEST(
  BufferContainerImportRequestBuilderTest, AcceptsBufferViewsWithElementRanges)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "accepts_buffer_views";
  const auto descriptor_path = dir / "Buffers" / "views.buffers.json";
  WriteText(descriptor_path,
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
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "rejects_explicit_all_view";
  const auto descriptor_path = dir / "Buffers" / "bad_view.buffers.json";
  WriteText(descriptor_path,
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
  EXPECT_THAT(errors.str(),
    ::testing::HasSubstr("buffer.container.schema_validation_failed"));
}

NOLINT_TEST(BufferContainerImportRequestBuilderTest,
  RejectsViewWithBothByteAndElementRanges)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "rejects_mixed_view_ranges";
  const auto descriptor_path = dir / "Buffers" / "mixed_view.buffers.json";
  WriteText(descriptor_path,
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
  EXPECT_THAT(errors.str(),
    ::testing::HasSubstr("buffer.container.schema_validation_failed"));
}

} // namespace
