//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/ImportManifest.cpp

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace {

using oxygen::content::import::ImportDiagnostic;
using oxygen::cooker::test::DiagnosticSummary;
using oxygen::cooker::test::HasDiagnostic;

using nlohmann::json;
using oxygen::content::import::ImportManifest;
using oxygen::cooker::test::WriteText;
using oxygen::testing::ScopedTempDir;

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
      "buffers": [
        {
          "source": "mesh_vertices.buffer.bin",
          "virtual_path": "/Cooked/Resources/Buffers/character_vertices.obuf",
          "element_stride": 32
        }
      ]
    })");

  const auto* const manifest_json = R"({
      "version": 1,
      "mounts": [{ "name": "Content", "path": "." }],
      "output": ".cooked",
      "defaults": {
        "buffer_container": {
          "name": "default-buffer-container-name"
        }
      },
      "jobs": [
        {
          "type": "buffer-container",
          "source": "Buffers/character.buffers.json",
          "name": "character-buffers-job"
        }
      ]
    })";
  WriteText(manifest_path, manifest_json);

  auto errors = std::vector<ImportDiagnostic> {};
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  ASSERT_HAS_VALUE(manifest) << DiagnosticSummary(errors);
  ASSERT_EQ(manifest->jobs.size(), 1U);

  auto request_errors = std::vector<ImportDiagnostic> {};
  const auto request = manifest->jobs.at(0).BuildRequest(request_errors);
  ASSERT_HAS_VALUE(request) << DiagnosticSummary(request_errors);
  ASSERT_HAS_VALUE(request->cooked_root);
  EXPECT_EQ(request->source_path, descriptor_path.lexically_normal());
  EXPECT_EQ(
    request->job_name, std::optional<std::string> { "character-buffers-job" });
  ASSERT_HAS_VALUE(request->buffer_container);

  const auto normalized
    = json::parse(request->buffer_container->descriptor_json);
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
          "virtual_path": "/Cooked/Resources/Buffers/lego_shared.obuf",
          "element_stride": 16
        }
      ]
    })");

  WriteText(manifest_path,
    R"({
      "version": 1,
      "mounts": [{ "name": "Content", "path": "." }],
      "output": "buffer-container-cooked",
      "jobs": [
        {
          "id": "prep.textures",
          "type": "texture",
          "source": "albedo.png",
          "virtual_path": "/Cooked/Textures/Albedo.otex"
        },
        {
          "id": "lego.buffers",
          "type": "buffer-container",
          "source": "Buffers/lego.buffers.json",
          "depends_on": ["prep.textures"]
        }
      ]
    })");

  auto errors = std::vector<ImportDiagnostic> {};
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  ASSERT_HAS_VALUE(manifest) << DiagnosticSummary(errors);
  ASSERT_EQ(manifest->jobs.size(), 2U);
  EXPECT_EQ(manifest->jobs.at(1).id, "lego.buffers");
  ASSERT_EQ(manifest->jobs.at(1).depends_on.size(), 1U);
  EXPECT_EQ(manifest->jobs.at(1).depends_on.at(0), "prep.textures");

  auto request_errors = std::vector<ImportDiagnostic> {};
  const auto request = manifest->jobs.at(1).BuildRequest(request_errors);
  ASSERT_HAS_VALUE(request) << DiagnosticSummary(request_errors);
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
      "mounts": [{ "name": "Content", "path": "." }],
      "jobs": [
        {
          "type": "buffer-container",
          "source": "Buffers/character.buffers.json",
          "intent": "albedo"
        }
      ]
    })");

  auto errors = std::vector<ImportDiagnostic> {};
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  EXPECT_FALSE(manifest.has_value());
  EXPECT_THAT(errors,
    HasDiagnostic(
      oxygen::content::import::manifest_codes::kSchemaValidationFailed));
}

} // namespace
