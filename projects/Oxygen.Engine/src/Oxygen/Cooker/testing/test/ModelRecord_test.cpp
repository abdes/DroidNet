//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>

#include <nlohmann/json.hpp>

#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Import/ModelImportRecord.h>
#include <Oxygen/Cooker/testing/ModelRecord.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::VirtualPathRuleSet;
using oxygen::content::import::ModelImportRecord;
using oxygen::cooker::testing::ModelRecord;
using oxygen::cooker::testing::UuidOf;

//! A record names its source from the record, carries the record's schema
//! version and its seed's source identity.
NOLINT_TEST(ModelRecordTest, NamesItsSourceFromTheRecord)
{
  const auto record
    = ModelRecord { "/Props/Models/Truck.glb", 4U }.Source("Truck.fbx");

  const auto document = nlohmann::json::parse(record.Text());

  EXPECT_EQ(record.SourcePath(), "/Props/Models/Truck.fbx");
  EXPECT_EQ(document.at("schema_version"), ModelImportRecord::kSchemaVersion);
  const auto source = oxygen::content::ResolveReference(record.Identity(),
    document.at("source").get<std::string>(), VirtualPathRuleSet::kCooked);
  ASSERT_TRUE(source.has_value());
  EXPECT_EQ(*source, "/Props/Models/Truck.fbx");
  EXPECT_EQ(document.at("source_identity"), UuidOf(4U).ToString());
}

//! A record imports the model's own file by default, with default settings.
NOLINT_TEST(ModelRecordTest, ImportsTheModelItselfByDefault)
{
  const auto record = ModelRecord { "/Props/Models/Truck.glb" };

  const auto document = nlohmann::json::parse(record.Text());

  EXPECT_EQ(record.SourcePath(), record.Identity());
  EXPECT_EQ(document.at("settings"), nlohmann::json::object());
}

} // namespace
