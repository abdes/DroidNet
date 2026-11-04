//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Tools/ImportTool/ReportJson.h>
#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::tool::test {

//! Checks a report a command wrote against the schema the SDK installs, and
//! that the schema and the producer agree on its version.
inline auto ExpectValidReport(const nlohmann::json& report) -> void
{
  static const auto schema = oxygen::cooker::test::LoadSchema(
    "Import/Schemas/oxygen.import-report.schema.json");
  EXPECT_THAT(
    oxygen::cooker::test::ValidateJson(schema, report), ::testing::IsEmpty());
  EXPECT_EQ(report.at("schema_version"), kReportSchemaVersion);
}

} // namespace oxygen::content::import::tool::test
