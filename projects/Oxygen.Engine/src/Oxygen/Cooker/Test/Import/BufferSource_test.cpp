//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/BufferSource.cpp

#include <algorithm>
#include <filesystem>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/BufferSource.h>
#include <Oxygen/Cooker/Test/Support/Diagnostics.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {
  using internal::BufferSource;
  using Json = nlohmann::basic_json<>;

  using oxygen::cooker::test::HasDiagnosticCode;

  NOLINT_TEST(BufferSourceTest, SeparatesRawInputFromDeclaredOutputWithoutIo)
  {
    const auto declarations = Json::parse(R"([{
      "source":"raw/../vertices.bin",
      "virtual_path":"/OtherMount/Buffers/vertices.obuf",
      "usage_flags":1,"element_stride":16,"alignment":32,
      "views":[{"name":"triangle","element_offset":0,"element_count":3}]
    }])");
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto sources = BufferSource::FromDeclarations(
      declarations, "authored/geometry.json", diagnostics);
    ASSERT_TRUE(sources.has_value()) << "Expected sources to contain a value";
    ASSERT_EQ(sources->size(), 1U);
    EXPECT_TRUE(diagnostics.empty());
    const auto& source = sources->front();
    EXPECT_EQ(
      source.source_path, std::filesystem::path("authored/vertices.bin"));
    EXPECT_EQ(source.source_id, "/OtherMount/Buffers/vertices.obuf");
    EXPECT_EQ(source.element_stride, 16U);
    EXPECT_EQ(source.alignment, 32U);
    ASSERT_EQ(source.view_specs.size(), 1U);
    EXPECT_EQ(source.view_specs.front().name, "triangle");
    EXPECT_EQ(source.view_specs.front().element_count, 3U);
  }

  NOLINT_TEST(BufferSourceTest, RejectsDuplicateOutputIdentity)
  {
    const auto declarations = Json::parse(R"([
      {"source":"a.bin","virtual_path":"/Content/Buffers/shared.obuf"},
      {"source":"b.bin","virtual_path":"/Content/Buffers/shared.obuf"}
    ])");
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(BufferSource::FromDeclarations(
      declarations, "geometry.json", diagnostics));
    EXPECT_TRUE(HasDiagnosticCode(
      diagnostics, "buffer.container.virtual_path_duplicate"));
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.front().object_path, "buffers[1].virtual_path");
  }

  NOLINT_TEST(BufferSourceTest, RejectsInvalidDeclarationWithSourceLocation)
  {
    const auto declarations = Json::parse(R"([{
      "source":"a.bin","virtual_path":"/Content/Buffers/a.obuf",
      "element_format":256
    }])");
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(BufferSource::FromDeclarations(
      declarations, "geometry.json", diagnostics, "lod.buffers"));
    EXPECT_TRUE(HasDiagnosticCode(
      diagnostics, "buffer.container.schema_validation_failed"));
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.front().source_path, "geometry.json");
    EXPECT_TRUE(diagnostics.front().object_path.starts_with("lod.buffers[0]"));
  }

  NOLINT_TEST(BufferSourceTest, PreservesFormatDrivenStride)
  {
    const auto declarations = Json::parse(R"([{
      "source":"a.bin","virtual_path":"/Content/Buffers/a.obuf",
      "element_format":1
    }])");
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto sources = BufferSource::FromDeclarations(
      declarations, "geometry.json", diagnostics);
    ASSERT_TRUE(sources.has_value()) << "Expected sources to contain a value";
    ASSERT_EQ(sources->size(), 1U);
    EXPECT_EQ(sources->front().element_stride, 0U);
    EXPECT_EQ(sources->front().element_format, 1U);
  }

  NOLINT_TEST(BufferSourceTest, ReportsWrongContainerType)
  {
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(BufferSource::FromDeclarations(
      Json::object(), "geometry.json", diagnostics));
    EXPECT_TRUE(
      HasDiagnosticCode(diagnostics, "buffer.container.buffers_missing"));
  }

  NOLINT_TEST(BufferSourceTest, RetainsDuplicateViewDiagnostic)
  {
    const auto declarations = Json::parse(R"([{
      "source":"a.bin","virtual_path":"/Content/Buffers/a.obuf",
      "views":[
        {"name":"surface","element_offset":0,"element_count":3},
        {"name":"surface","element_offset":3,"element_count":3}
      ]
    }])");
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(BufferSource::FromDeclarations(
      declarations, "geometry.json", diagnostics));
    EXPECT_TRUE(HasDiagnosticCode(diagnostics, "buffer.view.name_duplicate"));
    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(diagnostics.front().object_path, "buffers[0].views[1].name");
  }
} // namespace
} // namespace oxygen::content::import::test
