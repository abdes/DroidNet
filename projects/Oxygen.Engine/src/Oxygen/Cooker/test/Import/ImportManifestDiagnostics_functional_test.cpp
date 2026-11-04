//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/ImportManifest.cpp (typed, located diagnostics)

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace oxygen::content::import::test {

namespace {

  using nlohmann::json;
  using oxygen::cooker::test::WriteText;
  using oxygen::testing::ScopedTempDir;
  using ::testing::HasSubstr;

  //! A project folder whose manifests declare the `Content` mount on it.
  class ImportManifestDiagnosticsTest : public ::testing::Test {
  protected:
    //! What loading a manifest returned and reported.
    struct Outcome {
      std::optional<ImportManifest> manifest;
      std::vector<ImportDiagnostic> diagnostics;
    };

    [[nodiscard]] auto Directory() const -> const std::filesystem::path&
    {
      return temp_.Path();
    }

    static auto Manifest(json jobs) -> json
    {
      return {
        { "version", 1 },
        {
          "mounts",
          json::array({ { { "name", "Content" }, { "path", "." } } }),
        },
        { "jobs", std::move(jobs) },
      };
    }

    //! A script sidecar with inline bindings: a job that reads no file.
    static auto Sidecar(
      const std::string_view id, json depends_on = json::array()) -> json
    {
      return {
        { "type", "script-sidecar" },
        { "id", id },
        { "target_scene_virtual_path", "/Scenes/Test.oscene" },
        { "bindings", json::array() },
        { "depends_on", std::move(depends_on) },
      };
    }

    [[nodiscard]] auto ParseText(const std::string_view text) const -> Outcome
    {
      auto outcome = Outcome {};
      outcome.manifest
        = ImportManifest::Parse(text, Directory(), {}, outcome.diagnostics);
      return outcome;
    }

    [[nodiscard]] auto Parse(const json& manifest) const -> Outcome
    {
      return ParseText(manifest.dump());
    }

    //! The diagnostic with `code` of an outcome that holds one such.
    static auto Only(const Outcome& outcome, const std::string_view code)
      -> std::optional<ImportDiagnostic>
    {
      EXPECT_FALSE(outcome.manifest.has_value());
      const auto count = std::ranges::count(
        outcome.diagnostics, code, &ImportDiagnostic::code);
      EXPECT_EQ(count, 1) << code;
      const auto found
        = std::ranges::find(outcome.diagnostics, code, &ImportDiagnostic::code);
      if (found == outcome.diagnostics.end()) {
        return std::nullopt;
      }
      return *found;
    }

    //! Expects the outcome refused by the schema, every issue at `pointer`
    //! and one naming `key`: one key can break several schema rules.
    static auto ExpectKeyRefused(const Outcome& outcome,
      const json::json_pointer& pointer, const std::string_view key) -> void
    {
      EXPECT_FALSE(outcome.manifest.has_value());
      auto named = false;
      auto issues = 0;
      for (const auto& diagnostic : outcome.diagnostics) {
        if (diagnostic.code != manifest_codes::kSchemaValidationFailed) {
          continue;
        }
        ++issues;
        EXPECT_EQ(diagnostic.json_pointer, pointer.to_string())
          << diagnostic.message;
        named = named || diagnostic.message.contains(key);
      }
      EXPECT_GT(issues, 0);
      EXPECT_TRUE(named) << key;
    }

  private:
    ScopedTempDir temp_;
  };

  NOLINT_TEST_F(ImportManifestDiagnosticsTest, ReportsTextThatIsNotJson)
  {
    // Act
    const auto outcome = ParseText("{ \"jobs\": [");

    // Assert
    const auto found = Only(outcome, manifest_codes::kInvalidJson);
    ASSERT_HAS_VALUE(found);
    EXPECT_EQ(found->severity, ImportSeverity::kError);
    EXPECT_TRUE(found->json_pointer.empty());
    EXPECT_TRUE(found->source_path.empty());
  }

  NOLINT_TEST_F(ImportManifestDiagnosticsTest, ReportsAnUnknownRootKeyAtTheRoot)
  {
    // Arrange
    auto manifest = Manifest(json::array());
    manifest["unknown"] = 1;

    // Act
    const auto outcome = Parse(manifest);

    // Assert
    const auto found = Only(outcome, manifest_codes::kSchemaValidationFailed);
    ASSERT_HAS_VALUE(found);
    EXPECT_EQ(found->json_pointer, "");
    EXPECT_THAT(found->message, HasSubstr("unknown"));
  }

  NOLINT_TEST_F(ImportManifestDiagnosticsTest, ReportsAnUnknownJobKeyAtItsJob)
  {
    // Arrange
    auto second = Sidecar("second");
    second["unknown"] = true;

    // Act
    const auto outcome
      = Parse(Manifest(json::array({ Sidecar("first"), second })));

    // Assert
    ExpectKeyRefused(outcome, json::json_pointer { "/jobs/1" }, "unknown");
  }

  //! The schema's per-type key lists refuse what only an input job's own keys
  //! allow.
  NOLINT_TEST_F(ImportManifestDiagnosticsTest, ReportsAKeyAJobTypeDoesNotTake)
  {
    // Arrange
    const auto input = json {
      { "type", "input" },
      { "id", "actions" },
      { "source", "actions.input.json" },
      { "virtual_path", "/Content/Input/Actions.input" },
      { "verbose", true },
    };

    // Act
    const auto outcome = Parse(Manifest(json::array({ input })));

    // Assert
    ExpectKeyRefused(outcome, json::json_pointer { "/jobs/0" }, "verbose");
  }

  NOLINT_TEST_F(ImportManifestDiagnosticsTest, ReportsADuplicateJobId)
  {
    // Act
    const auto outcome = Parse(
      Manifest(json::array({ Sidecar("a"), Sidecar("b"), Sidecar("a") })));

    // Assert
    const auto found = Only(outcome, manifest_codes::kJobIdDuplicate);
    ASSERT_HAS_VALUE(found);
    EXPECT_EQ(found->json_pointer, "/jobs/2/id");
    EXPECT_THAT(found->message, HasSubstr("/jobs/0"));
  }

  //! A job of a later manifest names the manifest that declared it, and its
  //! index in that manifest.
  NOLINT_TEST_F(
    ImportManifestDiagnosticsTest, ReportsADuplicateJobIdAcrossABatch)
  {
    // Arrange
    const auto first = Directory() / "a.json";
    const auto second = Directory() / "b.json";
    WriteText(first, Manifest(json::array({ Sidecar("a") })).dump());
    WriteText(
      second, Manifest(json::array({ Sidecar("b"), Sidecar("a") })).dump());
    const auto paths = std::array { first, second };
    auto outcome = Outcome {};

    // Act
    outcome.manifest
      = ImportManifest::LoadBatch(paths, {}, outcome.diagnostics);

    // Assert
    const auto found = Only(outcome, manifest_codes::kJobIdDuplicate);
    ASSERT_HAS_VALUE(found);
    EXPECT_EQ(found->source_path, second.string());
    EXPECT_EQ(found->json_pointer, "/jobs/1/id");
    EXPECT_THAT(found->message, HasSubstr(first.string()));
  }

  NOLINT_TEST_F(ImportManifestDiagnosticsTest, ReportsAMissingDependency)
  {
    // Act
    const auto outcome = Parse(Manifest(json::array({
      Sidecar("a"),
      Sidecar("b", json::array({ "a", "nowhere" })),
    })));

    // Assert
    const auto found = Only(outcome, manifest_codes::kDependencyMissing);
    ASSERT_HAS_VALUE(found);
    EXPECT_EQ(found->json_pointer, "/jobs/1/depends_on/1");
    EXPECT_THAT(found->message, HasSubstr("nowhere"));
  }

  NOLINT_TEST_F(ImportManifestDiagnosticsTest, ReportsASelfDependency)
  {
    // Act
    const auto outcome = Parse(Manifest(
      json::array({ Sidecar("a"), Sidecar("b", json::array({ "b" })) })));

    // Assert
    const auto found = Only(outcome, manifest_codes::kDependencySelf);
    ASSERT_HAS_VALUE(found);
    EXPECT_EQ(found->json_pointer, "/jobs/1/depends_on/0");
    EXPECT_FALSE(std::ranges::contains(outcome.diagnostics,
      manifest_codes::kDependencyCycle, &ImportDiagnostic::code));
  }

  //! One cycle is one diagnostic at its first job, however many jobs it has.
  NOLINT_TEST_F(ImportManifestDiagnosticsTest, ReportsACycleOnceAtItsFirstJob)
  {
    // Act
    const auto outcome = Parse(Manifest(json::array({
      Sidecar("free"),
      Sidecar("a", json::array({ "b" })),
      Sidecar("b", json::array({ "c" })),
      Sidecar("c", json::array({ "a", "free" })),
    })));

    // Assert
    const auto found = Only(outcome, manifest_codes::kDependencyCycle);
    ASSERT_HAS_VALUE(found);
    EXPECT_EQ(found->json_pointer, "/jobs/1");
    EXPECT_THAT(found->message, HasSubstr("'a', 'b', 'c'"));
  }

  NOLINT_TEST_F(ImportManifestDiagnosticsTest, AcceptsAnAcyclicGraph)
  {
    // Act
    const auto outcome = Parse(Manifest(json::array({
      Sidecar("a"),
      Sidecar("b", json::array({ "a" })),
      Sidecar("c", json::array({ "a", "b" })),
    })));

    // Assert
    ASSERT_HAS_VALUE(outcome.manifest);
    EXPECT_TRUE(outcome.diagnostics.empty());
  }

  NOLINT_TEST_F(ImportManifestDiagnosticsTest, ReportsAnEmptyDependency)
  {
    // Act
    const auto outcome = Parse(Manifest(json::array({
      Sidecar("a"),
      Sidecar("b", json::array({ "a", "  " })),
    })));

    // Assert
    const auto found = Only(outcome, manifest_codes::kDependencyEmpty);
    ASSERT_HAS_VALUE(found);
    EXPECT_EQ(found->json_pointer, "/jobs/1/depends_on/1");
  }

  NOLINT_TEST_F(ImportManifestDiagnosticsTest, ReportsAnInputJobWithAnEmptyId)
  {
    // Arrange
    const auto input = json {
      { "type", "input" },
      { "id", "   " },
      { "source", "actions.input.json" },
      { "virtual_path", "/Content/Input/Actions.input" },
    };

    // Act
    const auto outcome = Parse(Manifest(json::array({ input })));

    // Assert
    const auto found = Only(outcome, manifest_codes::kInputJobIdMissing);
    ASSERT_HAS_VALUE(found);
    EXPECT_EQ(found->json_pointer, "/jobs/0/id");
  }

  NOLINT_TEST_F(ImportManifestDiagnosticsTest, ReportsAMissingVirtualPath)
  {
    // Arrange
    const auto material = json {
      { "type", "material-descriptor" },
      { "source", "ground.omat.json" },
    };

    // Act
    const auto outcome = Parse(Manifest(json::array({ material })));

    // Assert
    const auto found = Only(outcome, manifest_codes::kVirtualPathMissing);
    ASSERT_HAS_VALUE(found);
    EXPECT_EQ(found->json_pointer, "/jobs/0");
  }

  NOLINT_TEST_F(
    ImportManifestDiagnosticsTest, ReportsAVirtualPathOnAMultiAssetJob)
  {
    // Arrange
    auto sidecar = Sidecar("a");
    sidecar["virtual_path"] = "/Content/Scripts/A";

    // Act
    const auto outcome = Parse(Manifest(json::array({ sidecar })));

    // Assert
    const auto found = Only(outcome, manifest_codes::kVirtualPathNotAllowed);
    ASSERT_HAS_VALUE(found);
    EXPECT_EQ(found->json_pointer, "/jobs/0/virtual_path");
  }

  NOLINT_TEST_F(
    ImportManifestDiagnosticsTest, ReportsBindingsOnAJobWithoutSidecar)
  {
    // Arrange
    const auto script = json {
      { "type", "script" },
      { "source", "main.luau" },
      { "virtual_path", "/Content/Scripts/Main" },
      { "bindings", json::array() },
    };

    // Act
    const auto outcome = Parse(Manifest(json::array({ script })));

    // Assert
    const auto found = Only(outcome, manifest_codes::kBindingsNotAllowed);
    ASSERT_HAS_VALUE(found);
    EXPECT_EQ(found->json_pointer, "/jobs/0/bindings");
  }

  NOLINT_TEST_F(
    ImportManifestDiagnosticsTest, ReportsInstanceBindingsWithoutInlineBindings)
  {
    // Arrange
    const auto sidecar = json {
      { "type", "script-sidecar" },
      { "source", "scene.scripting-sidecar.json" },
      { "target_scene_virtual_path", "/Scenes/Test.oscene" },
      { "instance_bindings", json::array() },
    };

    // Act
    const auto outcome = Parse(Manifest(json::array({ sidecar })));

    // Assert
    const auto found
      = Only(outcome, manifest_codes::kInstanceBindingsNotAllowed);
    ASSERT_HAS_VALUE(found);
    EXPECT_EQ(found->json_pointer, "/jobs/0/instance_bindings");
  }

  NOLINT_TEST_F(ImportManifestDiagnosticsTest, ReportsAnAbsoluteCommittedFolder)
  {
    // Arrange
    auto sidecar = Sidecar("a");
    sidecar["output"] = Directory().generic_string();

    // Act
    const auto outcome = Parse(Manifest(json::array({ sidecar })));

    // Assert
    const auto found = Only(outcome, mount_codes::kAbsoluteCommitted.id);
    ASSERT_HAS_VALUE(found);
    EXPECT_EQ(found->json_pointer, "/jobs/0/output");
  }

  //! The schema checks the shape of `geometry_defaults`; these are the rules
  //! it cannot state.
  NOLINT_TEST_F(ImportManifestDiagnosticsTest, ReportsADecreasingLodPolicy)
  {
    // Arrange
    WriteText(Directory() / "model.gltf", "{}");
    const auto model = json {
      { "type", "gltf" },
      { "source", "model.gltf" },
      { "virtual_path", "/Content/Models/Model" },
      {
        "geometry_defaults",
        {
          {
            "Anchor",
            {
              {
                "lod_policy",
                {
                  { "type", "distance" },
                  { "thresholds", json::array({ 10.0, 5.0 }) },
                  { "hysteresis_ratio", 0.1 },
                },
              },
            },
          },
        },
      },
    };

    // Act
    const auto outcome = Parse(Manifest(json::array({ model })));

    // Assert
    const auto found = Only(outcome, manifest_codes::kLodPolicyInvalid);
    ASSERT_HAS_VALUE(found);
    EXPECT_EQ(
      found->json_pointer, "/jobs/0/geometry_defaults/Anchor/lod_policy");
  }

  NOLINT_TEST_F(ImportManifestDiagnosticsTest, ReportsANilSurfaceId)
  {
    // Arrange
    WriteText(Directory() / "model.gltf", "{}");
    const auto nil = std::string { "00000000-0000-0000-0000-000000000000" };
    const auto model = json {
      { "type", "gltf" },
      { "source", "model.gltf" },
      { "virtual_path", "/Content/Models/Model" },
      {
        "geometry_defaults",
        {
          {
            "Anchor",
            { { "surfaces", { { nil, { { "visible", false } } } } } },
          },
        },
      },
    };

    // Act
    const auto outcome = Parse(Manifest(json::array({ model })));

    // Assert
    const auto found = Only(outcome, manifest_codes::kSurfaceIdInvalid);
    ASSERT_HAS_VALUE(found);
    EXPECT_EQ(
      found->json_pointer, "/jobs/0/geometry_defaults/Anchor/surfaces/" + nil);
  }

  NOLINT_TEST_F(ImportManifestDiagnosticsTest, ReportsAManifestFileItCannotRead)
  {
    // Arrange
    const auto missing = Directory() / "missing.json";
    auto outcome = Outcome {};

    // Act
    outcome.manifest = ImportManifest::Load(missing, {}, outcome.diagnostics);

    // Assert
    const auto found = Only(outcome, manifest_codes::kReadFailed);
    ASSERT_HAS_VALUE(found);
    EXPECT_EQ(found->source_path, missing.string());
  }

  NOLINT_TEST_F(ImportManifestDiagnosticsTest, ReportsAnEmptyBatch)
  {
    // Arrange
    auto outcome = Outcome {};

    // Act
    outcome.manifest = ImportManifest::LoadBatch(
      std::span<const std::filesystem::path> {}, {}, outcome.diagnostics);

    // Assert
    const auto found = Only(outcome, manifest_codes::kBatchEmpty);
    ASSERT_HAS_VALUE(found);
  }

  //! A diagnostic of a manifest file names that file.
  NOLINT_TEST_F(ImportManifestDiagnosticsTest, LocatesADiagnosticInItsFile)
  {
    // Arrange
    const auto path = Directory() / "broken.json";
    WriteText(path,
      Manifest(json::array({ Sidecar("a", json::array({ "a" })) })).dump());
    auto outcome = Outcome {};

    // Act
    outcome.manifest = ImportManifest::Load(path, {}, outcome.diagnostics);

    // Assert
    const auto found = Only(outcome, manifest_codes::kDependencySelf);
    ASSERT_HAS_VALUE(found);
    EXPECT_EQ(found->source_path, path.string());
    EXPECT_EQ(found->json_pointer, "/jobs/0/depends_on/0");
  }

} // namespace

} // namespace oxygen::content::import::test
