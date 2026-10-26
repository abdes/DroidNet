//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.import-manifest.schema.json (execution
// settings)

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportConcurrency.h>
#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::content::import::AsyncImportService;
using oxygen::content::import::ImportConcurrency;
using oxygen::content::import::ImportPipelineConcurrency;
using oxygen::cooker::test::SchemaCase;

auto Schema() -> const json&
{
  static const auto schema = oxygen::cooker::test::LoadSchema(
    "Import/Schemas/oxygen.import-manifest.schema.json");
  return schema;
}

//! The default the schema states for one execution setting of a pipeline.
auto PipelineDefault(const std::string_view pipeline,
  const std::string_view setting) -> std::uint32_t
{
  return Schema()
    .at("properties")
    .at("concurrency")
    .at("properties")
    .at(pipeline)
    .at("properties")
    .at(setting)
    .at("default")
    .get<std::uint32_t>();
}

//! Each pipeline's workers and queue capacity in the schema are the values the
//! importer uses when a manifest leaves them unset, so a tool can show them.
NOLINT_TEST(ImportManifestSchemaTest, PipelineDefaultsAreTheImportersDefaults)
{
  struct Pipeline {
    std::string_view name;
    ImportPipelineConcurrency ImportConcurrency::* member;
  };
  constexpr auto kPipelines = std::array {
    Pipeline { .name = "texture", .member = &ImportConcurrency::texture },
    Pipeline { .name = "buffer", .member = &ImportConcurrency::buffer },
    Pipeline { .name = "material", .member = &ImportConcurrency::material },
    Pipeline { .name = "mesh_build", .member = &ImportConcurrency::mesh_build },
    Pipeline { .name = "geometry", .member = &ImportConcurrency::geometry },
    Pipeline { .name = "scene", .member = &ImportConcurrency::scene },
  };
  const auto defaults = ImportConcurrency {};
  const auto& pipelines
    = Schema().at("properties").at("concurrency").at("properties");
  EXPECT_EQ(pipelines.size(), kPipelines.size());
  for (const auto& pipeline : kPipelines) {
    SCOPED_TRACE(pipeline.name);
    const auto& expected = defaults.*pipeline.member;
    EXPECT_EQ(PipelineDefault(pipeline.name, "workers"), expected.workers);
    EXPECT_EQ(PipelineDefault(pipeline.name, "queue_capacity"),
      expected.queue_capacity);
  }
}

NOLINT_TEST(ImportManifestSchemaTest, AdmissionDefaultIsTheServiceDefault)
{
  EXPECT_EQ(Schema()
              .at("properties")
              .at("job_admission_capacity")
              .at("default")
              .get<std::uint32_t>(),
    AsyncImportService::Config {}.job_admission_capacity);
}

class ImportManifestExecutionSchemaTest
  : public ::testing::TestWithParam<SchemaCase> { };

NOLINT_TEST_P(ImportManifestExecutionSchemaTest, ValidatesExecutionSettings)
{
  oxygen::cooker::test::ExpectSchemaCase(Schema(), GetParam());
}

INSTANTIATE_TEST_SUITE_P(Cases, ImportManifestExecutionSchemaTest,
  ::testing::Values(
    SchemaCase {
      "AcceptsEverySetting",
      R"({"jobs":[],"thread_pool_size":8,"max_in_flight_jobs":4,
        "job_admission_capacity":16,
        "concurrency":{"texture":{"workers":4,"queue_capacity":128}}})",
      true,
      "",
    },
    SchemaCase {
      "AcceptsOneSettingOfAPipeline",
      R"({"jobs":[],"concurrency":{"geometry":{"workers":3},
        "scene":{"queue_capacity":4}}})",
      true,
      "",
    },
    SchemaCase {
      "RejectsZeroWorkers",
      R"({"jobs":[],"concurrency":{"buffer":{"workers":0}}})",
      false,
      "below minimum",
    },
    SchemaCase {
      "RejectsUnknownPipeline",
      R"({"jobs":[],"concurrency":{"audio":{"workers":1}}})",
      false,
      "audio",
    },
    SchemaCase {
      "RejectsUnknownPipelineSetting",
      R"({"jobs":[],"concurrency":{"scene":{"threads":1}}})",
      false,
      "threads",
    },
    SchemaCase {
      "RejectsZeroThreads",
      R"({"jobs":[],"thread_pool_size":0})",
      false,
      "below minimum",
    }),
  oxygen::cooker::test::SchemaCaseName);

} // namespace
