//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/ImportManifest.cpp (mounts, recorded sources),
// Import/Internal/ImportSession.cpp (producer)

#include <array>
#include <filesystem>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/ImportHarness.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  using nlohmann::json;
  using oxygen::cooker::test::HasDiagnosticCode;
  using oxygen::cooker::test::ImportServiceTest;
  using oxygen::cooker::test::WriteText;
  using testing::HasSubstr;
  using testing::Not;

  constexpr auto kGroundIdentity = std::string_view { "/Content/Ground.omat" };

  //! A project whose manifests declare the `Content` mount on `Content`.
  class RecordedSourceTest : public ImportServiceTest {
  protected:
    [[nodiscard]] auto ProjectRoot() const -> std::filesystem::path
    {
      return TempDir() / "project";
    }

    [[nodiscard]] auto ProjectCookedRoot() const -> std::filesystem::path
    {
      return ProjectRoot() / ".cooked";
    }

    //! Writes a material descriptor source below the `Content` mount.
    auto WriteMaterial(const std::string_view relative) const -> void
    {
      WriteText(
        ProjectRoot() / "Content" / relative, R"({ "name": "Ground" })");
    }

    static auto MaterialManifest(json jobs) -> json
    {
      return {
        { "version", 1 },
        {
          "mounts",
          json::array({ { { "name", "Content" }, { "path", "Content" } } }),
        },
        { "output", ".cooked" },
        { "jobs", std::move(jobs) },
      };
    }

    static auto MaterialJob(const std::string_view source,
      const std::string_view identity = kGroundIdentity) -> json
    {
      return {
        { "type", "material-descriptor" },
        { "source", source },
        { "virtual_path", identity },
      };
    }

    auto Load(const json& manifest, std::ostream& errors,
      const std::string_view name = "import-manifest.json") const
      -> std::optional<ImportManifest>
    {
      const auto path = ProjectRoot() / name;
      WriteText(path, manifest.dump(2));
      return ImportManifest::Load(path, {}, errors);
    }

    //! Cooks the manifest's only job.
    auto Cook(const json& manifest) -> std::optional<ImportReport>
    {
      auto errors = std::ostringstream {};
      const auto loaded = Load(manifest, errors);
      EXPECT_TRUE(loaded.has_value()) << errors.str();
      if (!loaded.has_value()) {
        return std::nullopt;
      }
      auto requests = loaded->BuildRequests(errors);
      EXPECT_EQ(requests.size(), 1U) << errors.str();
      if (requests.size() != 1U) {
        return std::nullopt;
      }
      return Import(std::move(requests.front()));
    }

    [[nodiscard]] auto RecordedSource(const std::string_view identity) const
      -> std::optional<std::string>
    {
      const auto index
        = lc::LooseCookedIndex::LoadFromRoot(ProjectCookedRoot());
      const auto key = index.FindAssetKeyByVirtualPath(identity);
      if (!key.has_value()) {
        return std::nullopt;
      }
      return index.FindSourcePath(*key).transform(
        [](const std::string_view source) { return std::string(source); });
    }
  };

  //! Two same-named sources in different folders cooked to one identity are
  //! two sources: the second is refused and the index keeps the first.
  NOLINT_TEST_F(RecordedSourceTest, SameNamedSourcesInTwoFoldersAreRefused)
  {
    // Arrange
    WriteMaterial("Examples/A/ground.omat.json");
    WriteMaterial("Examples/B/ground.omat.json");

    // Act
    const auto first = Cook(MaterialManifest(
      json::array({ MaterialJob("/Content/Examples/A/ground.omat.json") })));
    const auto second = Cook(MaterialManifest(
      json::array({ MaterialJob("/Content/Examples/B/ground.omat.json") })));

    // Assert
    ASSERT_HAS_VALUE(first);
    ASSERT_HAS_VALUE(second);
    EXPECT_TRUE(first->success);
    EXPECT_FALSE(second->success);
    EXPECT_TRUE(
      HasDiagnosticCode(second->diagnostics, "import.identity_collision"));
    EXPECT_EQ(RecordedSource(kGroundIdentity),
      std::optional<std::string>("/Content/Examples/A/ground.omat.json"));
  }

  //! The same file cooked again replaces its entry, whether the manifest names
  //! it by mount path or by a path relative to the project root.
  NOLINT_TEST_F(RecordedSourceTest, ReCookingTheSameFileReplacesItsEntry)
  {
    // Arrange
    WriteMaterial("Materials/ground.omat.json");

    // Act
    const auto first = Cook(MaterialManifest(
      json::array({ MaterialJob("/Content/Materials/ground.omat.json") })));
    const auto again = Cook(MaterialManifest(
      json::array({ MaterialJob("Content/Materials/ground.omat.json") })));

    // Assert
    ASSERT_HAS_VALUE(first);
    ASSERT_HAS_VALUE(again);
    EXPECT_TRUE(first->success);
    EXPECT_TRUE(again->success) << "a source re-cooking its asset replaces it";
    EXPECT_EQ(RecordedSource(kGroundIdentity),
      std::optional<std::string>("/Content/Materials/ground.omat.json"));
  }

  //! A request that no mount table located records no source and is refused.
  NOLINT_TEST_F(RecordedSourceTest, RequestWithoutRecordedSourceIsRefused)
  {
    // Arrange
    WriteMaterial("Materials/ground.omat.json");
    auto errors = std::ostringstream {};
    const auto loaded = Load(
      MaterialManifest(
        json::array({ MaterialJob("Content/Materials/ground.omat.json") })),
      errors);
    ASSERT_HAS_VALUE(loaded) << errors.str();
    auto requests = loaded->BuildRequests(errors);
    ASSERT_EQ(requests.size(), 1U);
    requests.front().source_mount_path.clear();

    // Act
    const auto report = Import(std::move(requests.front()));

    // Assert
    ASSERT_HAS_VALUE(report);
    EXPECT_FALSE(report->success);
    EXPECT_TRUE(
      HasDiagnosticCode(report->diagnostics, "import.source_outside_mount"));
  }

  //! One mount bound to different folders on two machines records the same
  //! source: the mount path, never the folder.
  NOLINT_TEST_F(
    RecordedSourceTest, OneMountBoundToTwoFoldersRecordsTheSameSource)
  {
    // Arrange
    const auto placeholder = json {
      { "version", 1 },
      { "mounts", json::array({ { { "name", "Studio" } } }) },
      {
        "jobs",
        json::array({
          {
            { "type", "material-descriptor" },
            { "source", "/Studio/Materials/Steel.omat.json" },
            { "virtual_path", "/Studio/Materials/Steel.omat" },
          },
        }),
      },
    };
    const auto here = TempDir() / "here";
    const auto agent = TempDir() / "agent";
    auto sources = std::vector<std::string> {};
    for (const auto& folder : std::array { here, agent }) {
      WriteText(
        folder / "Materials" / "Steel.omat.json", R"({ "name": "Steel" })");
      const auto machine
        = TempDir() / ("machine-" + folder.filename().string());
      WriteText(machine / ".mounts.json",
        json {
          { "version", 1 },
          { "mounts", { { "Studio", folder.generic_string() } } },
        }
          .dump());
      WriteText(machine / "import-manifest.json", placeholder.dump());
      auto errors = std::ostringstream {};

      // Act
      const auto loaded
        = ImportManifest::Load(machine / "import-manifest.json", {}, errors);

      ASSERT_HAS_VALUE(loaded) << errors.str();
      ASSERT_EQ(loaded->jobs.size(), 1U);
      sources.push_back(loaded->jobs.front().source_mount_path);
    }

    // Assert
    EXPECT_THAT(sources,
      testing::ElementsAre("/Studio/Materials/Steel.omat.json",
        "/Studio/Materials/Steel.omat.json"));
  }

  //! Every path a manifest commits is relative; an absolute one is refused by
  //! code, naming its field.
  NOLINT_TEST_F(RecordedSourceTest, AbsolutePathsInAManifestAreRefused)
  {
    WriteMaterial("Materials/ground.omat.json");
    const auto absolute
      = (ProjectRoot() / "Content" / "Materials" / "ground.omat.json")
          .generic_string();
    const auto cases = std::array {
      std::pair { "jobs[0].source",
        MaterialManifest(json::array({ MaterialJob(absolute) })) },
      std::pair { "output",
        [&] {
          auto manifest = MaterialManifest(json::array(
            { MaterialJob("/Content/Materials/ground.omat.json") }));
          manifest["output"] = ProjectCookedRoot().generic_string();
          return manifest;
        }() },
      std::pair { "jobs[0].cooked_context_roots[0]",
        [&] {
          auto job = MaterialJob("/Content/Materials/ground.omat.json");
          job["cooked_context_roots"]
            = json::array({ ProjectCookedRoot().generic_string() });
          return MaterialManifest(json::array({ job }));
        }() },
      std::pair { "project_root",
        [&] {
          auto manifest = MaterialManifest(json::array(
            { MaterialJob("/Content/Materials/ground.omat.json") }));
          manifest["project_root"] = ProjectRoot().generic_string();
          return manifest;
        }() },
    };
    for (const auto& [field, manifest] : cases) {
      auto errors = std::ostringstream {};

      const auto loaded = Load(manifest, errors);

      EXPECT_FALSE(loaded.has_value()) << field;
      EXPECT_THAT(errors.str(), HasSubstr("[path.absolute_committed]"))
        << field;
      EXPECT_THAT(errors.str(), HasSubstr(field)) << errors.str();
    }
  }

  //! A source in no source mount, or in a mount nothing binds, is refused by
  //! code when the manifest loads.
  NOLINT_TEST_F(RecordedSourceTest, SourcesOutsideBoundMountsAreRefused)
  {
    WriteText(ProjectRoot() / "Elsewhere" / "ground.omat.json",
      R"({ "name": "Ground" })");
    auto unbound = MaterialManifest(
      json::array({ MaterialJob("/Studio/Materials/Steel.omat.json") }));
    unbound["mounts"].push_back({ { "name", "Studio" } });
    const auto cases = std::array {
      std::pair { std::string_view { "import.source_outside_mount" },
        MaterialManifest(
          json::array({ MaterialJob("Elsewhere/ground.omat.json") })) },
      std::pair { std::string_view { "import.source_outside_mount" },
        MaterialManifest(
          json::array({ MaterialJob("/Nowhere/ground.omat.json") })) },
      std::pair { std::string_view { "import.mount_unbound" }, unbound },
    };
    for (const auto& [code, manifest] : cases) {
      auto errors = std::ostringstream {};

      const auto loaded = Load(manifest, errors);

      EXPECT_FALSE(loaded.has_value()) << code;
      EXPECT_THAT(errors.str(), HasSubstr("[" + std::string(code) + "]"))
        << errors.str();
    }
  }

  //! Manifests of one batch never map one mount name to two folders.
  NOLINT_TEST_F(RecordedSourceTest, ABatchRefusesOneNameForTwoFolders)
  {
    // Arrange
    WriteMaterial("Materials/ground.omat.json");
    auto other = MaterialManifest(
      json::array({ MaterialJob("/Content/ground.omat.json") }));
    other["mounts"]
      = json::array({ { { "name", "Content" }, { "path", "Other" } } });
    WriteText(
      ProjectRoot() / "Other" / "ground.omat.json", R"({ "name": "Ground" })");
    WriteText(ProjectRoot() / "a.json",
      MaterialManifest(
        json::array({ MaterialJob("/Content/Materials/ground.omat.json") }))
        .dump());
    WriteText(ProjectRoot() / "b.json", other.dump());
    const auto paths
      = std::array { ProjectRoot() / "a.json", ProjectRoot() / "b.json" };
    auto errors = std::ostringstream {};

    // Act
    const auto batch = ImportManifest::LoadBatch(paths, {}, errors);

    // Assert
    EXPECT_FALSE(batch.has_value());
    EXPECT_THAT(errors.str(), HasSubstr("[mount.name_conflict]"));
  }

  //! A source two manifests of one batch cook to one identity is cooked once,
  //! and a dependency on the dropped job follows the kept one.
  NOLINT_TEST_F(RecordedSourceTest, ABatchCooksASharedSourceOnce)
  {
    // Arrange
    WriteMaterial("Common/ground.omat.json");
    WriteMaterial("Materials/other.omat.json");
    auto shared = MaterialJob("/Content/Common/ground.omat.json");
    shared["id"] = "ground-a";
    WriteText(ProjectRoot() / "a.json",
      MaterialManifest(json::array({ shared })).dump());
    shared["id"] = "ground-b";
    auto dependent = MaterialJob(
      "/Content/Materials/other.omat.json", "/Content/Other.omat");
    dependent["id"] = "other";
    dependent["depends_on"] = json::array({ "ground-b" });
    WriteText(ProjectRoot() / "b.json",
      MaterialManifest(json::array({ shared, dependent })).dump());
    const auto paths
      = std::array { ProjectRoot() / "a.json", ProjectRoot() / "b.json" };
    auto errors = std::ostringstream {};

    // Act
    const auto batch = ImportManifest::LoadBatch(paths, {}, errors);

    // Assert
    ASSERT_HAS_VALUE(batch) << errors.str();
    ASSERT_EQ(batch->jobs.size(), 2U);
    EXPECT_EQ(batch->jobs.at(0).id, "ground-a");
    EXPECT_EQ(
      batch->jobs.at(1).depends_on, std::vector<std::string> { "ground-a" });
    EXPECT_THAT(errors.str(), Not(HasSubstr("ERROR")));
  }

  //! Two different jobs of one batch with one id are refused by code.
  NOLINT_TEST_F(RecordedSourceTest, ABatchRefusesOneIdForTwoJobs)
  {
    // Arrange
    WriteMaterial("Materials/ground.omat.json");
    WriteMaterial("Materials/other.omat.json");
    auto first = MaterialJob("/Content/Materials/ground.omat.json");
    first["id"] = "material";
    auto second = MaterialJob(
      "/Content/Materials/other.omat.json", "/Content/Other.omat");
    second["id"] = "material";
    WriteText(ProjectRoot() / "a.json",
      MaterialManifest(json::array({ first })).dump());
    WriteText(ProjectRoot() / "b.json",
      MaterialManifest(json::array({ second })).dump());
    const auto paths
      = std::array { ProjectRoot() / "a.json", ProjectRoot() / "b.json" };
    auto errors = std::ostringstream {};

    // Act
    const auto batch = ImportManifest::LoadBatch(paths, {}, errors);

    // Assert
    EXPECT_FALSE(batch.has_value());
    EXPECT_THAT(errors.str(), HasSubstr("[batch.job_id_duplicate]"));
  }

  //! A derived file in an output folder records the authored source it
  //! derives from, while the job reads the derived file.
  NOLINT_TEST_F(RecordedSourceTest, ADerivedFileRecordsItsAuthoredSource)
  {
    // Arrange
    WriteMaterial("Materials/ground.omat.json");
    WriteText(ProjectRoot() / ".pipeline" / "Materials" / "ground.omat.json",
      R"({ "name": "Ground" })");
    auto job = MaterialJob(".pipeline/Materials/ground.omat.json");
    job["derived_from"] = "/Content/Materials/ground.omat.json";

    // Act
    const auto report = Cook(MaterialManifest(json::array({ job })));

    // Assert
    ASSERT_HAS_VALUE(report);
    EXPECT_TRUE(report->success);
    EXPECT_EQ(RecordedSource(kGroundIdentity),
      std::optional<std::string>("/Content/Materials/ground.omat.json"));
  }

  //! `derived_from` is only for a derived file, and only names a mount path
  //! in a source mount; each misuse is refused by code.
  NOLINT_TEST_F(RecordedSourceTest, DerivedFromMisusesAreRefused)
  {
    WriteMaterial("Materials/ground.omat.json");
    WriteText(ProjectRoot() / ".pipeline" / "Materials" / "ground.omat.json",
      R"({ "name": "Ground" })");
    const auto derived
      = [](const std::string_view source, const std::string& from) -> json {
      auto job = MaterialJob(source);
      job["derived_from"] = from;
      return MaterialManifest(json::array({ job }));
    };
    const auto absolute
      = (ProjectRoot() / "Content" / "Materials" / "ground.omat.json")
          .generic_string();
    const auto cases = std::array {
      std::pair { std::string_view { "import.derived_from_not_derived" },
        derived("/Content/Materials/ground.omat.json",
          "/Content/Materials/ground.omat.json") },
      std::pair { std::string_view { "import.derived_from_invalid" },
        derived(".pipeline/Materials/ground.omat.json",
          "Content/Materials/ground.omat.json") },
      std::pair { std::string_view { "import.derived_from_invalid" },
        derived(".pipeline/Materials/ground.omat.json",
          "/Nowhere/Materials/ground.omat.json") },
      std::pair { std::string_view { "path.absolute_committed" },
        derived(".pipeline/Materials/ground.omat.json", absolute) },
      std::pair { std::string_view { "import.source_outside_mount" },
        MaterialManifest(json::array(
          { MaterialJob(".pipeline/Materials/ground.omat.json") })) },
    };
    for (const auto& [code, manifest] : cases) {
      auto errors = std::ostringstream {};

      const auto loaded = Load(manifest, errors);

      EXPECT_FALSE(loaded.has_value()) << code;
      EXPECT_THAT(errors.str(), HasSubstr("[" + std::string(code) + "]"))
        << errors.str();
    }
  }

} // namespace

} // namespace oxygen::content::import::test
