//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.buffer-container.schema.json

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::cooker::test::SchemaCase;

auto Schema() -> const json&
{
  static const auto schema = oxygen::cooker::test::LoadSchema(
    "Import/Schemas/oxygen.buffer-container.schema.json");
  return schema;
}

auto Accepts(const std::string_view name, const json& document) -> SchemaCase
{
  return SchemaCase {
    .name = name,
    .json = document.dump(),
    .valid = true,
    .error_substr = "",
  };
}

auto Rejects(const std::string_view name, const json& document,
  const std::string_view error) -> SchemaCase
{
  return SchemaCase {
    .name = name,
    .json = document.dump(),
    .valid = false,
    .error_substr = error,
  };
}

auto With(json document, const std::string_view pointer, json value) -> json
{
  document[json::json_pointer { std::string { pointer } }] = std::move(value);
  return document;
}

auto Without(json document, const std::string_view pointer) -> json
{
  const auto at = json::json_pointer { std::string { pointer } };
  document[at.parent_pointer()].erase(at.back());
  return document;
}

//! A schema constant, read from the schema.
auto Owned(const std::string_view pointer) -> const json&
{
  return Schema().at(json::json_pointer { std::string { pointer } });
}

constexpr auto kBufferMax
  = "/definitions/buffer_descriptor/properties/element_format/maximum";

auto Buffer() -> json
{
  return json {
    { "source", "positions.bin" },
    { "virtual_path", "Positions.obuf" },
  };
}

auto Valid() -> json
{
  return json { { "buffers", json::array({ Buffer() }) } };
}

auto ByteView() -> json
{
  return json {
    { "name", "Head" },
    { "byte_offset", 0 },
    { "byte_length", 4 },
  };
}

auto ElementView() -> json
{
  return json {
    { "name", "Tail" },
    { "element_offset", 1 },
    { "element_count", 2 },
  };
}

auto Maximal() -> json
{
  auto buffer = Buffer();
  buffer["usage_flags"] = 3;
  buffer["element_stride"] = 12;
  buffer["alignment"] = 16;
  buffer["views"] = json::array({ ByteView(), ElementView() });
  return json {
    { "$schema", "oxygen.buffer-container.schema.json" },
    { "name", "Mesh" },
    { "buffers", json::array({ buffer }) },
  };
}

auto WithView(json view) -> json
{
  return With(Valid(), "/buffers/0/views", json::array({ std::move(view) }));
}

class BufferContainerSchemaTest : public ::testing::TestWithParam<SchemaCase> {
};

NOLINT_TEST_P(BufferContainerSchemaTest, ValidatesDocument)
{
  oxygen::cooker::test::ExpectSchemaCase(Schema(), GetParam());
}

INSTANTIATE_TEST_SUITE_P(Cases, BufferContainerSchemaTest,
  ::testing::Values(
    // Accepted documents.
    Accepts("AcceptsMinimalDocument", Valid()),
    Accepts("AcceptsMaximalDocument", Maximal()),
    Accepts("AcceptsElementFormatWithoutStride",
      With(Valid(), "/buffers/0/element_format", 5)),
    Accepts("AcceptsElementFormatWithZeroStride",
      With(With(Valid(), "/buffers/0/element_format", 5),
        "/buffers/0/element_stride", 0)),
    Accepts("AcceptsElementFormatAtMaximum",
      With(Valid(), "/buffers/0/element_format", Owned(kBufferMax))),
    Accepts("AcceptsStrideWithoutFormat",
      With(Valid(), "/buffers/0/element_stride", 4)),
    Accepts("AcceptsByteView", WithView(ByteView())),
    Accepts("AcceptsElementView", WithView(ElementView())),

    // Required keys and unknown keys.
    Rejects("RejectsMissingBuffers", Without(Valid(), "/buffers"), "'buffers'"),
    Rejects("RejectsMissingSource", Without(Valid(), "/buffers/0/source"),
      "'source'"),
    Rejects("RejectsMissingVirtualPath",
      Without(Valid(), "/buffers/0/virtual_path"), "'virtual_path'"),
    Rejects("RejectsMissingViewName", WithView(Without(ByteView(), "/name")),
      "'name'"),
    Rejects("RejectsUnknownTopLevelField", With(Valid(), "/unknown", true),
      "'unknown'"),
    Rejects("RejectsUnknownBufferField",
      With(Valid(), "/buffers/0/unknown", true), "'unknown'"),
    Rejects("RejectsUnknownViewField",
      WithView(With(ByteView(), "/unknown", true)), "'unknown'"),

    // Wrong types.
    Rejects(
      "RejectsNonStringSchema", With(Valid(), "/$schema", 7), "/$schema: "),
    Rejects("RejectsNonStringName", With(Valid(), "/name", 7), "/name: "),
    Rejects(
      "RejectsNonArrayBuffers", With(Valid(), "/buffers", "x"), "/buffers: "),
    Rejects("RejectsNonStringSource", With(Valid(), "/buffers/0/source", 7),
      "/buffers/0/source: "),
    Rejects("RejectsNonIntegerUsageFlags",
      With(Valid(), "/buffers/0/usage_flags", "1"), "/buffers/0/usage_flags: "),
    Rejects("RejectsFractionalAlignment",
      With(Valid(), "/buffers/0/alignment", 1.5), "/buffers/0/alignment: "),
    Rejects("RejectsNonArrayViews", With(Valid(), "/buffers/0/views", "x"),
      "/buffers/0/views: "),
    Rejects("RejectsNonIntegerByteOffset",
      WithView(With(ByteView(), "/byte_offset", "0")),
      "/buffers/0/views/0/byte_offset: "),

    // Names and references.
    Rejects(
      "RejectsNameWithSeparator", With(Valid(), "/name", "a/b"), "/name: "),
    Rejects("RejectsEmptyName", With(Valid(), "/name", ""), "/name: "),
    Rejects("RejectsViewNameWithSeparator",
      WithView(With(ByteView(), "/name", "a/b")), "/buffers/0/views/0/name: "),
    Rejects("RejectsEmptySource", With(Valid(), "/buffers/0/source", ""),
      "/buffers/0/source: "),
    Rejects("RejectsVirtualPathWithoutBufferExtension",
      With(Valid(), "/buffers/0/virtual_path", "Positions.bin"),
      "/buffers/0/virtual_path: "),
    Rejects("RejectsVirtualPathThatIsNotAReference",
      With(Valid(), "/buffers/0/virtual_path", "Sub/../Positions.obuf"),
      "/buffers/0/virtual_path: "),

    // Numeric bounds.
    Rejects("RejectsEmptyBuffers", With(Valid(), "/buffers", json::array()),
      "/buffers: "),
    Rejects("RejectsNegativeUsageFlags",
      With(Valid(), "/buffers/0/usage_flags", -1), "/buffers/0/usage_flags: "),
    Rejects("RejectsNegativeElementStride",
      With(Valid(), "/buffers/0/element_stride", -1),
      "/buffers/0/element_stride: "),
    Rejects("RejectsNegativeElementFormat",
      With(Valid(), "/buffers/0/element_format", -1),
      "/buffers/0/element_format: "),
    Rejects("RejectsElementFormatAboveMaximum",
      With(Valid(), "/buffers/0/element_format",
        Owned(kBufferMax).get<std::int64_t>() + 1),
      "/buffers/0/element_format: "),
    Rejects("RejectsZeroAlignment", With(Valid(), "/buffers/0/alignment", 0),
      "/buffers/0/alignment: "),
    Rejects("RejectsEmptyViews",
      With(Valid(), "/buffers/0/views", json::array()), "/buffers/0/views: "),
    Rejects("RejectsDuplicateViews",
      With(
        Valid(), "/buffers/0/views", json::array({ ByteView(), ByteView() })),
      "/buffers/0/views: "),
    Rejects("RejectsNegativeByteOffset",
      WithView(With(ByteView(), "/byte_offset", -1)),
      "/buffers/0/views/0/byte_offset: "),
    Rejects("RejectsZeroByteLength",
      WithView(With(ByteView(), "/byte_length", 0)),
      "/buffers/0/views/0/byte_length: "),
    Rejects("RejectsNegativeElementOffset",
      WithView(With(ElementView(), "/element_offset", -1)),
      "/buffers/0/views/0/element_offset: "),
    Rejects("RejectsZeroElementCount",
      WithView(With(ElementView(), "/element_count", 0)),
      "/buffers/0/views/0/element_count: "),

    // View shape rules.
    Rejects("RejectsReservedViewName",
      WithView(With(ByteView(), "/name", "__all__")), "/buffers/0/views/0"),
    Rejects("RejectsViewWithoutRange", WithView(json { { "name", "Head" } }),
      "/buffers/0/views/0"),
    Rejects("RejectsByteViewWithoutLength",
      WithView(Without(ByteView(), "/byte_length")), "/buffers/0/views/0"),
    Rejects("RejectsElementViewWithoutCount",
      WithView(Without(ElementView(), "/element_count")), "/buffers/0/views/0"),
    Rejects("RejectsViewMixingByteAndElementRanges",
      WithView(With(ByteView(), "/element_count", 2)), "/buffers/0/views/0"),

    // Element layout rules.
    Rejects("RejectsFormatWithNonZeroStride",
      With(With(Valid(), "/buffers/0/element_format", 5),
        "/buffers/0/element_stride", 12),
      "/buffers/0"),
    Rejects("RejectsZeroStrideWithoutFormat",
      With(Valid(), "/buffers/0/element_stride", 0), "/buffers/0")),
  oxygen::cooker::test::SchemaCaseName);

} // namespace
