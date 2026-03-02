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

#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>

namespace {

using nlohmann::json;
using oxygen::content::import::EffectiveContentHashingEnabled;
using oxygen::content::import::ImportManifest;

auto MakeManifestPath(const std::string_view stem) -> std::filesystem::path
{
  auto dir = std::filesystem::temp_directory_path()
    / "oxygen_manifest_buffer_container";
  dir /= std::filesystem::path { std::string { stem } };
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir);
  return dir / "import_manifest.json";
}

auto WriteTextFile(
  const std::filesystem::path& path, const std::string_view text) -> void
{
  std::filesystem::create_directories(path.parent_path());
  auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(out.is_open());
  out << text;
}

NOLINT_TEST(ImportManifestBufferContainerTest,
  BuildsBufferContainerRequestWithDefaultsAndDescriptorOverrides)
{
  const auto manifest_path
    = MakeManifestPath("builds_buffer_container_request");
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Buffers" / "character.buffers.json";
  WriteTextFile(descriptor_path,
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
  WriteTextFile(manifest_path, manifest_json);

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs[0].BuildRequest(request_errors);
  ASSERT_TRUE(request.has_value()) << request_errors.str();
  ASSERT_TRUE(request->cooked_root.has_value());
  EXPECT_EQ(request->source_path, descriptor_path.lexically_normal());
  EXPECT_EQ(
    request->job_name, std::optional<std::string> { "character-buffers-job" });
  ASSERT_TRUE(request->buffer_container.has_value());

  // Descriptor-level content_hashing overrides manifest defaults/job settings.
  EXPECT_FALSE(
    EffectiveContentHashingEnabled(request->options.with_content_hashing));

  const auto normalized
    = json::parse(request->buffer_container->normalized_descriptor_json);
  EXPECT_EQ(normalized.at("name").get<std::string>(), "CharacterBuffers");
}

NOLINT_TEST(ImportManifestBufferContainerTest,
  CollectsAndPropagatesBufferContainerDependencies)
{
  const auto manifest_path
    = MakeManifestPath("collects_buffer_container_dependencies");
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Buffers" / "lego.buffers.json";
  WriteTextFile(descriptor_path,
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

  WriteTextFile(manifest_path,
    R"({
      "version": 1,
      "output": "C:/tmp/buffer-container-cooked",
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
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 2U);
  EXPECT_EQ(manifest->jobs[1].id, "lego.buffers");
  ASSERT_EQ(manifest->jobs[1].depends_on.size(), 1U);
  EXPECT_EQ(manifest->jobs[1].depends_on[0], "prep.textures");

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs[1].BuildRequest(request_errors);
  ASSERT_TRUE(request.has_value()) << request_errors.str();
  ASSERT_TRUE(request->orchestration.has_value());
  EXPECT_EQ(request->orchestration->job_id, "lego.buffers");
  ASSERT_EQ(request->orchestration->depends_on.size(), 1U);
  EXPECT_EQ(request->orchestration->depends_on[0], "prep.textures");
}

NOLINT_TEST(ImportManifestBufferContainerTest,
  RejectsBufferContainerJobWithDisallowedKeys)
{
  const auto manifest_path = MakeManifestPath("rejects_disallowed_key");
  WriteTextFile(manifest_path,
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
  EXPECT_TRUE(errors.str().find("manifest schema validation failed")
    != std::string::npos);
}

} // namespace
