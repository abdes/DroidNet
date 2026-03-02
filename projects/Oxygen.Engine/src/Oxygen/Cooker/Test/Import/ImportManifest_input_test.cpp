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

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/ImportManifest.h>

namespace {

using oxygen::content::import::ImportManifest;

auto MakeManifestPath(const std::string_view stem) -> std::filesystem::path
{
  auto dir = std::filesystem::temp_directory_path() / "oxygen_manifest_input";
  dir /= std::filesystem::path { std::string { stem } };
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir);
  return dir / "import_manifest.json";
}

auto WriteManifestFile(
  const std::filesystem::path& path, const std::string& json_text) -> void
{
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(out.is_open());
  out << json_text;
}

NOLINT_TEST(ImportManifestInputTest, AcceptsInputJobWithDependencies)
{
  const auto manifest_path = MakeManifestPath("accepts_input_job");

  WriteManifestFile(manifest_path,
    R"({
      "version": 1,
      "jobs": [
        {
          "id": "core.actions",
          "type": "input",
          "source": "Content/Input/Core.input.json"
        },
        {
          "id": "vehicle.contexts",
          "type": "input",
          "source": "Content/Input/Vehicle.input.json",
          "depends_on": ["core.actions"]
        }
      ]
    })");

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 2U);

  std::ostringstream req_errors;
  const auto request = manifest->jobs[1].BuildRequest(req_errors);
  ASSERT_TRUE(request.has_value()) << req_errors.str();
  EXPECT_TRUE(request->input.has_value());
  ASSERT_TRUE(request->orchestration.has_value());
  EXPECT_EQ(request->orchestration->job_id, "vehicle.contexts");
  ASSERT_EQ(request->orchestration->depends_on.size(), 1U);
  EXPECT_EQ(request->orchestration->depends_on[0], "core.actions");
}

NOLINT_TEST(ImportManifestInputTest, RejectsInputJobWithDisallowedKeys)
{
  const auto manifest_path = MakeManifestPath("rejects_input_extra_keys");

  WriteManifestFile(manifest_path,
    R"({
      "version": 1,
      "jobs": [
        {
          "id": "core.actions",
          "type": "input",
          "source": "Content/Input/Core.input.json",
          "verbose": true
        }
      ]
    })");

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  EXPECT_FALSE(manifest.has_value());
  EXPECT_TRUE(
    errors.str().find("input.manifest.key_not_allowed") != std::string::npos);
}

NOLINT_TEST(ImportManifestInputTest, InputOutputPrecedenceIsConsistent)
{
  const auto manifest_path = MakeManifestPath("input_output_precedence");

  WriteManifestFile(manifest_path,
    R"({
      "version": 1,
      "output": "C:/tmp/top-level",
      "defaults": {
        "input": {
          "output": "C:/tmp/default-input"
        }
      },
      "jobs": [
        {
          "id": "core.actions",
          "type": "input",
          "source": "Content/Input/Core.input.json",
          "output": "C:/tmp/job-input"
        }
      ]
    })");

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  std::ostringstream req_errors;
  const auto request = manifest->jobs[0].BuildRequest(req_errors);
  ASSERT_TRUE(request.has_value()) << req_errors.str();
  ASSERT_TRUE(request->cooked_root.has_value());
  EXPECT_EQ(request->cooked_root->generic_string(), "C:/tmp/job-input");
}

NOLINT_TEST(ImportManifestInputTest, InputDefaultsOverrideTopLevelOutput)
{
  const auto manifest_path
    = MakeManifestPath("input_defaults_override_top_level");

  WriteManifestFile(manifest_path,
    R"({
      "version": 1,
      "output": "C:/tmp/top-level",
      "defaults": {
        "input": {
          "output": "C:/tmp/default-input"
        }
      },
      "jobs": [
        {
          "id": "core.actions",
          "type": "input",
          "source": "Content/Input/Core.input.json"
        }
      ]
    })");

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  std::ostringstream req_errors;
  const auto request = manifest->jobs[0].BuildRequest(req_errors);
  ASSERT_TRUE(request.has_value()) << req_errors.str();
  ASSERT_TRUE(request->cooked_root.has_value());
  EXPECT_EQ(request->cooked_root->generic_string(), "C:/tmp/default-input");
}

NOLINT_TEST(ImportManifestInputTest, InputFallsBackToTopLevelOutput)
{
  const auto manifest_path = MakeManifestPath("input_top_level_fallback");

  WriteManifestFile(manifest_path,
    R"({
      "version": 1,
      "output": "C:/tmp/top-level",
      "jobs": [
        {
          "id": "core.actions",
          "type": "input",
          "source": "Content/Input/Core.input.json"
        }
      ]
    })");

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  std::ostringstream req_errors;
  const auto request = manifest->jobs[0].BuildRequest(req_errors);
  ASSERT_TRUE(request.has_value()) << req_errors.str();
  ASSERT_TRUE(request->cooked_root.has_value());
  EXPECT_EQ(request->cooked_root->generic_string(), "C:/tmp/top-level");
}

NOLINT_TEST(ImportManifestInputTest, InputUsesSharedLayoutDefaultsByDefault)
{
  const auto manifest_path = MakeManifestPath("input_shared_layout_defaults");

  WriteManifestFile(manifest_path,
    R"({
      "version": 1,
      "jobs": [
        {
          "id": "core.actions",
          "type": "input",
          "source": "Content/Input/Core.input.json"
        }
      ]
    })");

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  std::ostringstream req_errors;
  const auto request = manifest->jobs[0].BuildRequest(req_errors);
  ASSERT_TRUE(request.has_value()) << req_errors.str();
  EXPECT_EQ(request->loose_cooked_layout.input_subdir, "Input");
}

NOLINT_TEST(ImportManifestInputTest, InputHonorsManifestLayoutOverrides)
{
  const auto manifest_path = MakeManifestPath("input_layout_overrides");

  WriteManifestFile(manifest_path,
    R"({
      "version": 1,
      "layout": {
        "input_subdir": "InputCustom"
      },
      "defaults": {
        "layout": {
          "input_subdir": "InputOverride"
        }
      },
      "jobs": [
        {
          "id": "core.actions",
          "type": "input",
          "source": "Content/Input/Core.input.json"
        }
      ]
    })");

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  std::ostringstream req_errors;
  const auto request = manifest->jobs[0].BuildRequest(req_errors);
  ASSERT_TRUE(request.has_value()) << req_errors.str();
  EXPECT_EQ(request->loose_cooked_layout.input_subdir, "InputOverride");
}

NOLINT_TEST(ImportManifestInputTest, RejectsInputJobWithoutId)
{
  const auto manifest_path = MakeManifestPath("rejects_input_missing_id");

  WriteManifestFile(manifest_path,
    R"({
      "version": 1,
      "jobs": [
        {
          "type": "input",
          "source": "Content/Input/Core.input.json"
        }
      ]
    })");

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  EXPECT_FALSE(manifest.has_value());
  EXPECT_TRUE(
    errors.str().find("input.manifest.job_id_missing") != std::string::npos);
}

} // namespace
