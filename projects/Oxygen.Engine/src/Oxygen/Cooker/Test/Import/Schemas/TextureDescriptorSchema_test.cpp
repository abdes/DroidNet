//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.texture-descriptor.schema.json

#include <string>

#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Test/Support/JsonSchema.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::cooker::test::LoadSchema;
using oxygen::cooker::test::SchemaCase;

auto Schema() -> const json&
{
  static const auto schema
    = LoadSchema("Import/Schemas/oxygen.texture-descriptor.schema.json");
  return schema;
}

class TextureDescriptorSchemaTest
  : public ::testing::TestWithParam<SchemaCase> { };

NOLINT_TEST_P(TextureDescriptorSchemaTest, ValidatesDocument)
{
  oxygen::cooker::test::ExpectSchemaCase(Schema(), GetParam());
}

INSTANTIATE_TEST_SUITE_P(Cases, TextureDescriptorSchemaTest,
  ::testing::Values(
    SchemaCase {
      "AcceptsCanonicalDocument",
      R"({
    "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.texture-descriptor.schema.json",
    "source": "Textures/brick_albedo.png",
    "intent": "albedo",
    "decode": {
      "color_space": "srgb",
      "flip_y": false,
      "force_rgba": true
    },
    "mips": {
      "policy": "full",
      "filter": "kaiser",
      "renormalize": true
    },
    "output": {
      "format": "bc7_srgb",
      "bc7_quality": "high",
      "packing_policy": "d3d12"
    },
    "hdr": {
      "handling": "tonemap",
      "exposure_ev": 0.0,
      "bake_hdr": false
    },
    "cube": {
      "cubemap": false
    }
  })",
      true,
      "",
    },
    SchemaCase {
      "RejectsRemovedMipFilterSpace",
      R"({
    "source": "Textures/color.png", "mips": { "filter_space": "srgb" }
  })",
      false,
      "'filter_space'",
    },
    SchemaCase {
      "RejectsUnknownNestedFields",
      R"({
    "source": "Textures/brick_albedo.png",
    "decode": {
      "unknown_setting": true
    }
  })",
      false,
      "'unknown_setting'",
    },
    SchemaCase {
      "RequiresCubeFaceSizeWhenEquirectToCubeEnabled",
      R"({
    "source": "Textures/env_panorama.hdr",
    "cube": {
      "equirect_to_cube": true
    }
  })",
      false,
      "'cube_face_size'",
    }),
  oxygen::cooker::test::SchemaCaseName);

} // namespace
