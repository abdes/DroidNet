//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ModelImportRecord.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace fs = std::filesystem;
using nlohmann::json;
using oxygen::Uuid;
using oxygen::content::import::CommittedPath;
using oxygen::content::import::ImportDiagnostic;
using oxygen::content::import::ModelImportRecord;
using oxygen::content::import::ModelImportRecordContent;
using oxygen::content::import::Mount;
using oxygen::content::import::MountKind;
using oxygen::content::import::MountOrigin;
using oxygen::content::import::MountRole;
using oxygen::content::import::MountScope;
using oxygen::content::import::MountState;
using oxygen::content::import::MountTable;
using oxygen::cooker::test::DiagnosticSummary;
using oxygen::cooker::test::HasDiagnosticCode;

//! Runs `ModelImportRecordCases.json`, which the editor's record reader also
//! runs, over the table's mounts, held in memory under a root no test
//! creates.
class ModelImportRecordCasesTest : public testing::Test {
protected:
  static auto Cases() -> const json&
  {
    static const auto cases = [] -> json {
      auto stream = std::ifstream { OXYGEN_COOKER_MODEL_IMPORT_RECORD_CASES };
      if (!stream) {
        throw std::runtime_error("ModelImportRecordCases.json is missing");
      }
      return json::parse(stream);
    }();
    return cases;
  }

  static auto Root() -> fs::path
  {
    return fs::current_path().root_path() / "OxygenModelImportRecordCases";
  }

  static auto Mounts() -> const MountTable&
  {
    static const auto table = [] -> MountTable {
      auto mounts = MountTable {
        .project_root = Root() / "Game",
        .declared_in = "ModelImportRecordCases.json",
        .mounts = {},
        .diagnostics = {},
      };
      for (const auto& mount : Cases().at("mounts")) {
        mounts.mounts.push_back(Mount {
          .name = mount.at("name").get<std::string>(),
          .kind = MountKind::kSource,
          .scope = MountScope::kDeclared,
          .declared_path = std::nullopt,
          .folder = Root() / mount.at("folder").get<std::string>(),
          .origin = MountOrigin::kCommitted,
          .origin_file = {},
          .state = MountState::kBound,
          .role = MountRole::kOwn,
          .findings = {},
        });
      }
      return mounts;
    }();
    return table;
  }

  static auto RecordOf(const std::string& identity) -> fs::path
  {
    auto findings = std::vector<ImportDiagnostic> {};
    const auto path
      = ModelImportRecord::RecordPathOf(Mounts(), identity, findings);
    if (!path.has_value()) {
      throw std::invalid_argument(DiagnosticSummary(findings));
    }
    return *path;
  }
};

//! A record is at its model's identity, and its path gives that identity
//! back.
NOLINT_TEST_F(ModelImportRecordCasesTest, RecordsLiveAtTheirIdentity)
{
  for (const auto& row : Cases().at("records")) {
    SCOPED_TRACE(row.at("name").get<std::string>());
    auto findings = std::vector<ImportDiagnostic> {};
    if (row.contains("refusal")) {
      const auto refusal = row.at("refusal").get<std::string>();
      if (row.contains("record")) {
        EXPECT_FALSE(ModelImportRecord::IdentityOf(
          Mounts(), Root() / row.at("record").get<std::string>(), findings));
      } else {
        EXPECT_FALSE(ModelImportRecord::RecordPathOf(
          Mounts(), row.at("identity").get<std::string>(), findings));
      }
      EXPECT_TRUE(HasDiagnosticCode(findings, refusal))
        << DiagnosticSummary(findings);
      continue;
    }
    const auto record = Root() / row.at("record").get<std::string>();
    const auto identity = row.at("identity").get<std::string>();

    const auto found
      = ModelImportRecord::IdentityOf(Mounts(), record, findings);
    const auto path
      = ModelImportRecord::RecordPathOf(Mounts(), identity, findings);

    EXPECT_EQ(found, identity) << DiagnosticSummary(findings);
    ASSERT_HAS_VALUE(path) << DiagnosticSummary(findings);
    EXPECT_EQ(fs::path(*path).lexically_normal(), record.lexically_normal());
  }
}

//! A record spells its source from the record and locates it back.
NOLINT_TEST_F(ModelImportRecordCasesTest, SourcesAreReferencesFromTheRecord)
{
  for (const auto& row : Cases().at("sources")) {
    SCOPED_TRACE(row.at("name").get<std::string>());
    const auto identity = row.at("identity").get<std::string>();
    const auto written = row.at("written").get<std::string>();
    const auto record = RecordOf(identity);
    auto findings = std::vector<ImportDiagnostic> {};

    const auto located = Mounts().LocateReference(record,
      CommittedPath { .text = written, .json_pointer = "/source" }, findings);

    if (row.contains("refusal")) {
      EXPECT_FALSE(located.has_value());
      ASSERT_TRUE(
        HasDiagnosticCode(findings, row.at("refusal").get<std::string>()))
        << DiagnosticSummary(findings);
      if (row.contains("vpath")) {
        EXPECT_NE(
          findings.front().message.find(row.at("vpath").get<std::string>()),
          std::string::npos)
          << findings.front().message;
      }
      continue;
    }
    const auto source = row.at("source").get<std::string>();
    ASSERT_HAS_VALUE(located) << DiagnosticSummary(findings);
    EXPECT_EQ(located->mount_path, source);

    auto content = ModelImportRecordContent {};
    content.source_mount_path = source;
    content.source_identity = Uuid::Generate();
    const auto text = ModelImportRecord::Serialize(content, identity);
    EXPECT_EQ(json::parse(text).at("source"), written);
  }
}

} // namespace
