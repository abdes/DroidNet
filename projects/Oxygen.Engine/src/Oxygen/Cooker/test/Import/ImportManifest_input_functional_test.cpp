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

#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/TempDir.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::ImportManifest;
using oxygen::cooker::test::ScopedTempDir;
using oxygen::cooker::test::WriteText;

NOLINT_TEST(ImportManifestInputTest, AcceptsInputJobWithDependencies)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "accepts_input_job" / "import_manifest.json";

  WriteText(manifest_path,
    R"({
      "version": 1,
      "jobs": [
        {
          "id": "core.actions",
          "type": "input",
          "source": "Content/Input/Core.input.json",
          "virtual_path": "/Content/Input/Core.input"
        },
        {
          "id": "vehicle.contexts",
          "type": "input",
          "source": "Content/Input/Vehicle.input.json",
          "virtual_path": "/Content/Input/Vehicle.input",
          "depends_on": ["core.actions"]
        }
      ]
    })");

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_HAS_VALUE(manifest) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 2U);

  std::ostringstream req_errors;
  const auto request = manifest->jobs.at(1).BuildRequest(req_errors);
  ASSERT_HAS_VALUE(request) << req_errors.str();
  EXPECT_TRUE(request->input.has_value());
  ASSERT_HAS_VALUE(request->orchestration);
  EXPECT_EQ(request->orchestration->job_id, "vehicle.contexts");
  ASSERT_EQ(request->orchestration->depends_on.size(), 1U);
  EXPECT_EQ(request->orchestration->depends_on.at(0), "core.actions");
}

NOLINT_TEST(ImportManifestInputTest, RejectsInputJobWithDisallowedKeys)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "rejects_input_extra_keys" / "import_manifest.json";

  WriteText(manifest_path,
    R"({
      "version": 1,
      "jobs": [
        {
          "id": "core.actions",
          "type": "input",
          "source": "Content/Input/Core.input.json",
          "virtual_path": "/Content/Input/Core.input",
          "verbose": true
        }
      ]
    })");

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  EXPECT_FALSE(manifest.has_value());
  EXPECT_THAT(
    errors.str(), ::testing::HasSubstr("input.manifest.key_not_allowed"));
}

NOLINT_TEST(ImportManifestInputTest, InputOutputPrecedenceIsConsistent)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "input_output_precedence" / "import_manifest.json";

  const auto top_level = temp.Path() / "out" / "top-level";
  const auto default_input = temp.Path() / "out" / "default-input";
  const auto job_input = temp.Path() / "out" / "job-input";

  WriteText(manifest_path,
    R"({
      "version": 1,
      "output": ")"
      + top_level.generic_string() + R"(",
      "defaults": {
        "input": {
          "output": ")"
      + default_input.generic_string() + R"("
        }
      },
      "jobs": [
        {
          "id": "core.actions",
          "type": "input",
          "source": "Content/Input/Core.input.json",
          "virtual_path": "/Content/Input/Core.input",
          "output": ")"
      + job_input.generic_string() + R"("
        }
      ]
    })");

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_HAS_VALUE(manifest) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  std::ostringstream req_errors;
  const auto request = manifest->jobs.at(0).BuildRequest(req_errors);
  ASSERT_HAS_VALUE(request) << req_errors.str();
  ASSERT_HAS_VALUE(request->cooked_root);
  EXPECT_EQ(*request->cooked_root, job_input);
}

NOLINT_TEST(ImportManifestInputTest, InputDefaultsOverrideTopLevelOutput)
{
  const ScopedTempDir temp;
  const auto manifest_path = temp.Path() / "input_defaults_override_top_level"
    / "import_manifest.json";

  const auto top_level = temp.Path() / "out" / "top-level";
  const auto default_input = temp.Path() / "out" / "default-input";

  WriteText(manifest_path,
    R"({
      "version": 1,
      "output": ")"
      + top_level.generic_string() + R"(",
      "defaults": {
        "input": {
          "output": ")"
      + default_input.generic_string() + R"("
        }
      },
      "jobs": [
        {
          "id": "core.actions",
          "type": "input",
          "source": "Content/Input/Core.input.json",
          "virtual_path": "/Content/Input/Core.input"
        }
      ]
    })");

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_HAS_VALUE(manifest) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  std::ostringstream req_errors;
  const auto request = manifest->jobs.at(0).BuildRequest(req_errors);
  ASSERT_HAS_VALUE(request) << req_errors.str();
  ASSERT_HAS_VALUE(request->cooked_root);
  EXPECT_EQ(*request->cooked_root, default_input);
}

NOLINT_TEST(ImportManifestInputTest, InputFallsBackToTopLevelOutput)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "input_top_level_fallback" / "import_manifest.json";

  const auto top_level = temp.Path() / "out" / "top-level";

  WriteText(manifest_path,
    R"({
      "version": 1,
      "output": ")"
      + top_level.generic_string() + R"(",
      "jobs": [
        {
          "id": "core.actions",
          "type": "input",
          "source": "Content/Input/Core.input.json",
          "virtual_path": "/Content/Input/Core.input"
        }
      ]
    })");

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_HAS_VALUE(manifest) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  std::ostringstream req_errors;
  const auto request = manifest->jobs.at(0).BuildRequest(req_errors);
  ASSERT_HAS_VALUE(request) << req_errors.str();
  ASSERT_HAS_VALUE(request->cooked_root);
  EXPECT_EQ(*request->cooked_root, top_level);
}

NOLINT_TEST(
  ImportManifestInputTest, ResolvesRelativeTopLevelOutputAgainstManifest)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "relative_top_level_output" / "import_manifest.json";
  const auto expected_cooked_root
    = (manifest_path.parent_path() / ".cooked" / "top-level")
        .lexically_normal();

  WriteText(manifest_path,
    R"({
      "version": 1,
      "output": ".cooked/top-level",
      "jobs": [
        {
          "id": "core.actions",
          "type": "input",
          "source": "Content/Input/Core.input.json",
          "virtual_path": "/Content/Input/Core.input"
        }
      ]
    })");

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_HAS_VALUE(manifest) << errors.str();

  std::ostringstream req_errors;
  const auto request = manifest->jobs.at(0).BuildRequest(req_errors);
  ASSERT_HAS_VALUE(request) << req_errors.str();
  ASSERT_HAS_VALUE(request->cooked_root);
  EXPECT_EQ(request->cooked_root->lexically_normal(), expected_cooked_root);
}

NOLINT_TEST(ImportManifestInputTest, ResolvesRelativeJobOutputAgainstManifest)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "relative_job_output" / "import_manifest.json";
  const auto expected_cooked_root
    = (manifest_path.parent_path() / ".cooked" / "job-level")
        .lexically_normal();

  WriteText(manifest_path,
    R"({
      "version": 1,
      "output": ".cooked/top-level",
      "defaults": {
        "input": {
          "output": ".cooked/default-level"
        }
      },
      "jobs": [
        {
          "id": "core.actions",
          "type": "input",
          "source": "Content/Input/Core.input.json",
          "virtual_path": "/Content/Input/Core.input",
          "output": ".cooked/job-level"
        }
      ]
    })");

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_HAS_VALUE(manifest) << errors.str();

  std::ostringstream req_errors;
  const auto request = manifest->jobs.at(0).BuildRequest(req_errors);
  ASSERT_HAS_VALUE(request) << req_errors.str();
  ASSERT_HAS_VALUE(request->cooked_root);
  EXPECT_EQ(request->cooked_root->lexically_normal(), expected_cooked_root);
}

//! An input job names the source's identity; its products are named under it.
NOLINT_TEST(ImportManifestInputTest, CarriesInputSourceIdentity)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "input_identity" / "import_manifest.json";

  WriteText(manifest_path,
    R"({
      "version": 1,
      "layout": { "virtual_mount_root": "/Content" },
      "jobs": [
        {
          "id": "input:Gameplay/Player.input.json",
          "type": "input",
          "source": "Content/Gameplay/Player.input.json",
          "virtual_path": "/Content/Gameplay/Player.input"
        }
      ]
    })");

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_HAS_VALUE(manifest) << errors.str();

  std::ostringstream req_errors;
  const auto request = manifest->jobs.at(0).BuildRequest(req_errors);
  ASSERT_HAS_VALUE(request) << req_errors.str();
  EXPECT_EQ(request->asset_virtual_path, "/Content/Gameplay/Player.input");
}

//! An input job reads the actions of other sources from its cooked context
//! roots, as a scene reads its references.
NOLINT_TEST(ImportManifestInputTest, CarriesCookedContextRoots)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "input_context_roots" / "import_manifest.json";
  const auto library = (temp.Path() / "library").lexically_normal();

  WriteText(manifest_path,
    R"({
      "version": 1,
      "jobs": [
        {
          "id": "player",
          "type": "input",
          "source": "Content/Input/Player.input.json",
          "virtual_path": "/Content/Input/Player.input",
          "cooked_context_roots": [")"
      + library.generic_string() + R"("]
        }
      ]
    })");

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_HAS_VALUE(manifest) << errors.str();

  std::ostringstream req_errors;
  const auto request = manifest->jobs.at(0).BuildRequest(req_errors);
  ASSERT_HAS_VALUE(request) << req_errors.str();
  ASSERT_EQ(request->cooked_context_roots.size(), 1U);
  EXPECT_EQ(request->cooked_context_roots.front(), library);
}

//! An input job without its identity is refused: its products have no name.
NOLINT_TEST(ImportManifestInputTest, RejectsInputJobWithoutVirtualPath)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "rejects_input_missing_identity" / "import_manifest.json";

  WriteText(manifest_path,
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
  EXPECT_FALSE(manifest.has_value());
  EXPECT_THAT(
    errors.str(), ::testing::HasSubstr("import.manifest.virtual_path_missing"));
}

NOLINT_TEST(ImportManifestInputTest, RejectsInputJobWithoutId)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "rejects_input_missing_id" / "import_manifest.json";

  WriteText(manifest_path,
    R"({
      "version": 1,
      "jobs": [
        {
          "type": "input",
          "source": "Content/Input/Core.input.json",
          "virtual_path": "/Content/Input/Core.input"
        }
      ]
    })");

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  EXPECT_FALSE(manifest.has_value());
  EXPECT_THAT(
    errors.str(), ::testing::HasSubstr("input.manifest.job_id_missing"));
}

} // namespace
