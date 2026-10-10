//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/ImportManifest.cpp

#include <filesystem>
#include <optional>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Cooker/Test/Support/TestValues.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::content::import::EffectiveContentHashingEnabled;
using oxygen::content::import::ImportManifest;
using oxygen::cooker::test::kContentHashingDefault;
using oxygen::cooker::test::ScopedTempDir;
using oxygen::cooker::test::WriteText;

NOLINT_TEST(ImportManifestBufferContainerTest,
  BuildsBufferContainerRequestWithDefaultsAndDescriptorOverrides)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "builds_buffer_container_request" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Buffers" / "character.buffers.json";
  WriteText(descriptor_path,
    R"({
      "name": "CharacterBuffers",
      "content_hashing": false,
      "buffers": [
        {
          "source": "mesh_vertices.buffer.bin",
          "virtual_path": "/.cooked/Resources/Buffers/character_vertices.obuf",
          "element_stride": 32
        }
      ]
    })");

  const auto cooked_root = (root / ".cooked").generic_string();
  const auto manifest_json = std::string { R"({
      "version": 1,
      "output": ")" }
    + cooked_root + R"(",
      "defaults": {
        "buffer_container": {
          "content_hashing": true,
          "name": "default-buffer-container-name"
        }
      },
      "jobs": [
        {
          "type": "buffer-container",
          "source": "Buffers/character.buffers.json",
          "name": "character-buffers-job",
          "content_hashing": true
        }
      ]
    })";
  WriteText(manifest_path, manifest_json);

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_HAS_VALUE(manifest) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs.at(0).BuildRequest(request_errors);
  ASSERT_HAS_VALUE(request) << request_errors.str();
  ASSERT_HAS_VALUE(request->cooked_root);
  EXPECT_EQ(request->source_path, descriptor_path.lexically_normal());
  EXPECT_EQ(
    request->job_name, std::optional<std::string> { "character-buffers-job" });
  ASSERT_HAS_VALUE(request->buffer_container);

  // Descriptor-level content_hashing overrides manifest defaults/job settings.
  // Release must hash authored content even when the descriptor opts out.
  EXPECT_EQ(request->options.with_content_hashing, kContentHashingDefault);

  const auto normalized
    = json::parse(request->buffer_container->normalized_descriptor_json);
  EXPECT_EQ(normalized.at("name").get<std::string>(), "CharacterBuffers");
}

NOLINT_TEST(ImportManifestBufferContainerTest,
  CollectsAndPropagatesBufferContainerDependencies)
{
  const ScopedTempDir temp;
  const auto manifest_path = temp.Path()
    / "collects_buffer_container_dependencies" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Buffers" / "lego.buffers.json";
  WriteText(descriptor_path,
    R"({
      "name": "LegoBuffers",
      "buffers": [
        {
          "source": "lego.buffer.bin",
          "virtual_path": "/.cooked/Resources/Buffers/lego_shared.obuf",
          "element_stride": 16
        }
      ]
    })");

  WriteText(manifest_path,
    R"({
      "version": 1,
      "output": ")"
      + (manifest_path.parent_path() / "buffer-container-cooked")
        .generic_string()
      + R"(",
      "jobs": [
        {
          "id": "prep.textures",
          "type": "texture",
          "source": "albedo.png"
        },
        {
          "id": "lego.buffers",
          "type": "buffer-container",
          "source": "Buffers/lego.buffers.json",
          "depends_on": ["prep.textures"]
        }
      ]
    })");

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_HAS_VALUE(manifest) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 2U);
  EXPECT_EQ(manifest->jobs.at(1).id, "lego.buffers");
  ASSERT_EQ(manifest->jobs.at(1).depends_on.size(), 1U);
  EXPECT_EQ(manifest->jobs.at(1).depends_on.at(0), "prep.textures");

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs.at(1).BuildRequest(request_errors);
  ASSERT_HAS_VALUE(request) << request_errors.str();
  ASSERT_HAS_VALUE(request->orchestration);
  EXPECT_EQ(request->orchestration->job_id, "lego.buffers");
  ASSERT_EQ(request->orchestration->depends_on.size(), 1U);
  EXPECT_EQ(request->orchestration->depends_on.at(0), "prep.textures");
}

NOLINT_TEST(ImportManifestBufferContainerTest,
  RejectsBufferContainerJobWithDisallowedKeys)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "rejects_disallowed_key" / "import_manifest.json";
  WriteText(manifest_path,
    R"({
      "version": 1,
      "jobs": [
        {
          "type": "buffer-container",
          "source": "Buffers/character.buffers.json",
          "intent": "albedo"
        }
      ]
    })");

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  EXPECT_FALSE(manifest.has_value());
  EXPECT_THAT(
    errors.str(), ::testing::HasSubstr("manifest schema validation failed"));
}

} // namespace
