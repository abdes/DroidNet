//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/Schemas/oxygen.pak-catalog.schema.json

#include <string>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::cooker::test::LoadSchema;
using oxygen::cooker::test::ValidateJson;
using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

auto Schema() -> const json&
{
  static const auto schema
    = LoadSchema("Pak/Schemas/oxygen.pak-catalog.schema.json");
  return schema;
}

NOLINT_TEST(PakCatalogSchemaTest, AcceptsCanonicalCatalogDocument)
{
  const auto doc = json::parse(R"({
    "$schema": "./src/Oxygen/Cooker/Pak/Schemas/oxygen.pak-catalog.schema.json",
    "schema_version": 5,
    "mount": "Props",
    "deleted": [],
    "bases": [],
    "dependencies": [
      {
        "mount": "Engine",
        "requirements": [
          {
            "kind": "asset",
            "key": "22222222-3333-4444-5555-666666666666",
            "type": "Material"
          },
          { "kind": "texture", "key": "33333333-4444-5555-6666-777777777777" }
        ]
      }
    ],
    "source_key": "01234567-89ab-7def-8123-456789abcdef",
    "content_version": 42,
    "catalog_digest": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
    "entries": [
      {
        "asset_key": "11111111-2222-3333-4444-555555555555",
        "asset_type": "Material",
        "descriptor_digest": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "transitive_resource_digest": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
      },
      {
        "asset_key": "66666666-7777-8888-9999-aaaaaaaaaaaa",
        "asset_type": "PhysicsScene",
        "descriptor_digest": "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
        "transitive_resource_digest": "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
      }
    ]
  })");

  EXPECT_THAT(ValidateJson(Schema(), doc), IsEmpty());
}

NOLINT_TEST(PakCatalogSchemaTest, RejectsUnknownAssetTypeAndUnknownFields)
{
  const auto doc = json::parse(R"({
    "schema_version": 5,
    "mount": "Props",
    "deleted": [],
    "bases": [],
    "dependencies": [],
    "source_key": "01234567-89ab-7def-8123-456789abcdef",
    "content_version": 7,
    "catalog_digest": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
    "entries": [
      {
        "asset_key": "11111111-2222-3333-4444-555555555555",
        "asset_type": "__Unknown__",
        "descriptor_digest": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "transitive_resource_digest": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
        "unexpected": true
      }
    ]
  })");

  const auto errors = ValidateJson(Schema(), doc);
  EXPECT_THAT(errors, Contains(HasSubstr("/entries/0/asset_type: ")));
  EXPECT_THAT(errors, Contains(HasSubstr("'unexpected'")));
}

NOLINT_TEST(PakCatalogSchemaTest, RejectsNonCanonicalKeyAndDigestFormats)
{
  const auto doc = json::parse(R"({
    "schema_version": 5,
    "mount": "Props",
    "deleted": [],
    "bases": [],
    "dependencies": [],
    "source_key": "01234567-89ab-6def-8123-456789abcdef",
    "content_version": 7,
    "catalog_digest": "XYZ",
    "entries": [
      {
        "asset_key": "11111111-2222-3333-4444-555555555555",
        "asset_type": "Scene",
        "descriptor_digest": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "transitive_resource_digest": "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
      }
    ]
  })");

  const auto errors = ValidateJson(Schema(), doc);
  EXPECT_THAT(errors, Contains(HasSubstr("/source_key: ")));
  EXPECT_THAT(errors, Contains(HasSubstr("/catalog_digest: ")));
  EXPECT_THAT(errors, Contains(HasSubstr("/entries/0/descriptor_digest: ")));
}

//! A requirement is an asset with its type or a texture without one, and a
//! dependency requires something of a named mount.
NOLINT_TEST(PakCatalogSchemaTest, RejectsMalformedDependencies)
{
  const auto doc = json::parse(R"({
    "schema_version": 5,
    "mount": "Props",
    "deleted": [],
    "bases": [],
    "dependencies": [
      {
        "mount": "Engine",
        "requirements": [
          { "kind": "asset", "key": "22222222-3333-4444-5555-666666666666" },
          {
            "kind": "texture",
            "key": "33333333-4444-5555-6666-777777777777",
            "type": "Material"
          }
        ]
      },
      { "mount": "Vehicles", "requirements": [] },
      { "mount": "1Bad", "requirements": [] }
    ],
    "source_key": "01234567-89ab-7def-8123-456789abcdef",
    "content_version": 7,
    "catalog_digest": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
    "entries": []
  })");

  const auto errors = ValidateJson(Schema(), doc);
  EXPECT_THAT(errors, Contains(HasSubstr("/dependencies/0/requirements/0")));
  EXPECT_THAT(errors, Contains(HasSubstr("/dependencies/0/requirements/1")));
  EXPECT_THAT(errors, Contains(HasSubstr("/dependencies/1/requirements")));
  EXPECT_THAT(errors, Contains(HasSubstr("/dependencies/2/mount")));
}

} // namespace
