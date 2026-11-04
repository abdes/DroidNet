//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.import-manifest.schema.json (execution
// settings)

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportConcurrency.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
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
      oxygen::cooker::test::ManifestText(
        R"({"jobs":[],"thread_pool_size":8,"max_in_flight_jobs":4,
        "job_admission_capacity":16,
        "concurrency":{"texture":{"workers":4,"queue_capacity":128}}})"),
      true,
      "",
    },
    SchemaCase {
      "AcceptsOneSettingOfAPipeline",
      oxygen::cooker::test::ManifestText(
        R"({"jobs":[],"concurrency":{"geometry":{"workers":3},
        "scene":{"queue_capacity":4}}})"),
      true,
      "",
    },
    SchemaCase {
      "RejectsZeroWorkers",
      oxygen::cooker::test::ManifestText(
        R"({"jobs":[],"concurrency":{"buffer":{"workers":0}}})"),
      false,
      "below minimum",
    },
    SchemaCase {
      "RejectsUnknownPipeline",
      oxygen::cooker::test::ManifestText(
        R"({"jobs":[],"concurrency":{"audio":{"workers":1}}})"),
      false,
      "audio",
    },
    SchemaCase {
      "RejectsUnknownPipelineSetting",
      oxygen::cooker::test::ManifestText(
        R"({"jobs":[],"concurrency":{"scene":{"threads":1}}})"),
      false,
      "threads",
    },
    SchemaCase {
      "RejectsZeroThreads",
      oxygen::cooker::test::ManifestText(R"({"jobs":[],"thread_pool_size":0})"),
      false,
      "below minimum",
    },
    SchemaCase {
      "AcceptsASourceLimitPastFourGigabytes",
      oxygen::cooker::test::ManifestText(
        R"({"jobs":[],"max_source_bytes":5000000000})"),
      true,
      "",
    },
    SchemaCase {
      "RejectsAZeroSourceLimit",
      oxygen::cooker::test::ManifestText(R"({"jobs":[],"max_source_bytes":0})"),
      false,
      "below minimum",
    }),
  oxygen::cooker::test::SchemaCaseName);

class ImportManifestMountsSchemaTest
  : public ::testing::TestWithParam<SchemaCase> { };

NOLINT_TEST_P(ImportManifestMountsSchemaTest, ValidatesMountDeclarations)
{
  oxygen::cooker::test::ExpectSchemaCase(Schema(), GetParam());
}

INSTANTIATE_TEST_SUITE_P(Cases, ImportManifestMountsSchemaTest,
  ::testing::Values(
    SchemaCase {
      "AcceptsCookedAndPlaceholderMounts",
      oxygen::cooker::test::ManifestText(
        R"({"jobs":[],"mounts":[{"name":"Content","path":"."},
        {"name":"Engine","kind":"Cooked"},{"name":"Art"}]})"),
      true,
      "",
    },
    SchemaCase {
      "AcceptsMountsWithoutLayout",
      oxygen::cooker::test::ManifestText(
        R"({"jobs":[],"mounts":[{"name":"Content","path":"."}]})"),
      true,
      "",
    },
    SchemaCase {
      "RejectsMountWithUnknownKey",
      oxygen::cooker::test::ManifestText(
        R"({"jobs":[],"mounts":[{"name":"Content","path":".","colour":"red"}]})"),
      false,
      "colour",
    },
    SchemaCase {
      "RejectsMountWithoutName",
      oxygen::cooker::test::ManifestText(
        R"({"jobs":[],"mounts":[{"path":"."}]})"),
      false,
      "name",
    }),
  oxygen::cooker::test::SchemaCaseName);

//! The schema's defaults for a model import state what the options default to.
NOLINT_TEST(ImportManifestSchemaTest, ModelDefaultsMatchTheImportOptions)
{
  const auto options = oxygen::content::import::ImportOptions {};
  for (const auto* section : { "scene_settings", "job_settings" }) {
    const auto& properties
      = Schema().at("definitions").at(section).at("properties");
    const auto number = [&](const char* key) -> float {
      return properties.at(key).at("default").get<float>();
    };
    const auto flag = [&](const char* key) -> bool {
      return properties.at(key).at("default").get<bool>();
    };
    EXPECT_FLOAT_EQ(
      number("omitted_light_range_m"), options.omitted_light_range_m)
      << section;
    EXPECT_FLOAT_EQ(number("normals_smoothing_angle_deg"),
      options.normals_smoothing_angle_deg)
      << section;
    EXPECT_FLOAT_EQ(
      number("light_intensity_scale"), options.light_intensity_scale)
      << section;
    EXPECT_EQ(flag("import_lights"), options.import_lights) << section;
    EXPECT_EQ(flag("import_cameras"), options.import_cameras) << section;
    EXPECT_EQ(flag("generate_lods"), options.lod_generation.enabled) << section;
    EXPECT_EQ(properties.at("lod_levels").at("default").get<std::uint32_t>(),
      options.lod_generation.levels)
      << section;
    EXPECT_FLOAT_EQ(
      number("lod_reduction_percent"), options.lod_generation.reduction_percent)
      << section;
  }
}

//! A model job takes the settings of a model import, not a descriptor's.
NOLINT_TEST(ImportManifestSchemaTest, ModelJobRefusesDescriptorKeys)
{
  auto validator
    = oxygen::content::import::internal::MakeJsonSchemaValidator(Schema());
  const auto refused
    = [&validator](const char* key, const json& value) -> bool {
    auto job = json {
      { "type", "gltf" },
      { "source", "a.glb" },
      { "virtual_path", "/Content/a" },
    };
    job[key] = value;
    const auto manifest = json {
      {
        "schema_version",
        oxygen::content::import::kImportManifestSchemaVersion,
      },
      { "jobs", json::array({ job }) },
    };
    auto issues
      = std::vector<oxygen::content::import::internal::JsonSchemaIssue> {};
    auto failure = std::string {};
    EXPECT_TRUE(oxygen::content::import::internal::CollectJsonSchemaIssues(
      validator, manifest, issues, failure))
      << failure;
    return !issues.empty();
  };

  EXPECT_FALSE(refused("import_lights", false));
  EXPECT_FALSE(refused("max_size", 512));
  EXPECT_TRUE(refused("sources", json::array()));
  EXPECT_TRUE(refused("derived_from", "/Content/a.glb"));
  EXPECT_TRUE(refused("compile", true));
  EXPECT_TRUE(refused("physics_backend", "jolt"));
}

} // namespace
