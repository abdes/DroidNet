//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.texture-descriptor.schema.json

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::cooker::test::LoadSchema;
using oxygen::cooker::test::SchemaCase;
using oxygen::cooker::test::TextureDescriptor;

auto Schema() -> const json&
{
  static const auto schema
    = LoadSchema("Import/Schemas/oxygen.texture-descriptor.schema.json");
  return schema;
}

auto Texture(const std::string_view identity, const std::string_view source)
  -> TextureDescriptor
{
  return TextureDescriptor { std::string { identity }, source };
}

auto BrickAlbedo() -> TextureDescriptor
{
  return Texture(
    "/Content/Textures/brick_albedo.otex", "Textures/brick_albedo.png");
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
      BrickAlbedo()
        .Set("/$schema",
          "./src/Oxygen/Cooker/Import/Schemas/"
          "oxygen.texture-descriptor.schema.json")
        .Set("/intent", "albedo")
        .Set("/decode/color_space", "srgb")
        .Set("/decode/flip_y", false)
        .Set("/decode/force_rgba", true)
        .Set("/mips/policy", "full")
        .Set("/mips/filter", "kaiser")
        .Set("/mips/renormalize", true)
        .Set("/output/format", "bc7_srgb")
        .Set("/output/bc7_quality", "high")
        .Set("/output/packing_policy", "d3d12")
        .Set("/hdr/handling", "tonemap")
        .Set("/hdr/exposure_ev", 0.0)
        .Set("/hdr/bake_hdr", false)
        .Set("/cube/cubemap", false)
        .Text(),
      true,
      "",
    },
    SchemaCase {
      "RejectsRemovedMipFilterSpace",
      Texture("/Content/Textures/color.otex", "Textures/color.png")
        .Set("/mips/filter_space", "srgb")
        .Text(),
      false,
      "'filter_space'",
    },
    SchemaCase {
      "RejectsStaleVirtualPath",
      BrickAlbedo()
        .Set("/virtual_path", "/Content/Textures/brick_albedo.otex")
        .Text(),
      false,
      "'virtual_path'",
    },
    SchemaCase {
      "AcceptsRelativeSourceReferences",
      Texture("/Content/Textures/brick.otex", "../Sources/brick.png")
        .Append("/sources", { { "file", "./faces/px.png" } })
        .Text(),
      true,
      "",
    },
    SchemaCase {
      "RejectsSourceWithEmptySegment",
      Texture("/Content/Textures/brick.otex", "Sources//brick.png").Text(),
      false,
      "/source",
    },
    SchemaCase {
      "RejectsUnknownNestedFields",
      BrickAlbedo().Set("/decode/unknown_setting", true).Text(),
      false,
      "'unknown_setting'",
    },
    SchemaCase {
      "RequiresCubeFaceSizeWhenEquirectToCubeEnabled",
      Texture(
        "/Content/Textures/env_panorama.otex", "Textures/env_panorama.hdr")
        .Set("/cube/equirect_to_cube", true)
        .Text(),
      false,
      "'cube_face_size'",
    }),
  oxygen::cooker::test::SchemaCaseName);

} // namespace
