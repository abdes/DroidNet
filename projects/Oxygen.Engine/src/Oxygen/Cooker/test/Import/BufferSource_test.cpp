//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/BufferSource.cpp

#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/BufferSource.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/TempDir.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {
  using internal::BufferSource;
  using Json = nlohmann::basic_json<>;

  using oxygen::cooker::test::ContentAndOtherMountTable;
  using oxygen::cooker::test::ContentMountTable;
  using oxygen::cooker::test::DiagnosticSummary;
  using oxygen::cooker::test::HasDiagnosticCode;
  using oxygen::cooker::test::TempDirTest;

  //! The project's `Content` mount is the temporary root.
  class BufferSourceTest : public TempDirTest {
  protected:
    auto Mounts() -> const MountTable&
    {
      if (!mounts_) {
        mounts_ = ContentMountTable(TempDir());
      }
      return *mounts_;
    }

  private:
    std::shared_ptr<const MountTable> mounts_;
  };

  NOLINT_TEST_F(BufferSourceTest, SeparatesRawInputFromDeclaredOutputWithoutIo)
  {
    const auto declarations = Json::parse(R"([{
      "source":"raw/../vertices.bin",
      "virtual_path":"/OtherMount/Buffers/vertices.obuf",
      "usage_flags":1,"element_stride":16,"alignment":32,
      "views":[{"name":"triangle","element_offset":0,"element_count":3}]
    }])");
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto sources = BufferSource::FromDeclarations(
      declarations, "authored/geometry.json", Mounts(), diagnostics);
    ASSERT_HAS_VALUE(sources) << "Expected sources to contain a value";
    ASSERT_EQ(sources->size(), 1U);
    EXPECT_TRUE(diagnostics.empty());
    const auto& source = sources->front();
    EXPECT_EQ(source.source_path,
      std::filesystem::weakly_canonical(TempDir() / "authored/vertices.bin"));
    EXPECT_EQ(source.source_id, "/OtherMount/Buffers/vertices.obuf");
    EXPECT_EQ(source.element_stride, 16U);
    EXPECT_EQ(source.alignment, 32U);
    ASSERT_EQ(source.view_specs.size(), 1U);
    EXPECT_EQ(source.view_specs.front().name, "triangle");
    EXPECT_EQ(source.view_specs.front().element_count, 3U);
  }

  NOLINT_TEST_F(BufferSourceTest, RejectsDuplicateOutputIdentity)
  {
    const auto declarations = Json::parse(R"([
      {"source":"a.bin","virtual_path":"/Content/Buffers/shared.obuf"},
      {"source":"b.bin","virtual_path":"/Content/Buffers/shared.obuf"}
    ])");
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(BufferSource::FromDeclarations(
      declarations, "geometry.json", Mounts(), diagnostics));
    EXPECT_TRUE(HasDiagnosticCode(
      diagnostics, "buffer.container.virtual_path_duplicate"));
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.front().object_path, "buffers[1].virtual_path");
  }

  NOLINT_TEST_F(BufferSourceTest, RejectsInvalidDeclarationWithSourceLocation)
  {
    const auto declarations = Json::parse(R"([{
      "source":"a.bin","virtual_path":"/Content/Buffers/a.obuf",
      "element_format":256
    }])");
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(BufferSource::FromDeclarations(
      declarations, "geometry.json", Mounts(), diagnostics, "lod.buffers"));
    EXPECT_TRUE(HasDiagnosticCode(
      diagnostics, "buffer.container.schema_validation_failed"));
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.front().source_path, "geometry.json");
    EXPECT_TRUE(diagnostics.front().object_path.starts_with("lod.buffers[0]"));
  }

  NOLINT_TEST_F(BufferSourceTest, PreservesFormatDrivenStride)
  {
    const auto declarations = Json::parse(R"([{
      "source":"a.bin","virtual_path":"/Content/Buffers/a.obuf",
      "element_format":1
    }])");
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto sources = BufferSource::FromDeclarations(
      declarations, "geometry.json", Mounts(), diagnostics);
    ASSERT_HAS_VALUE(sources) << "Expected sources to contain a value";
    ASSERT_EQ(sources->size(), 1U);
    EXPECT_EQ(sources->front().element_stride, 0U);
    EXPECT_EQ(sources->front().element_format, 1U);
  }

  NOLINT_TEST_F(BufferSourceTest, ReportsWrongContainerType)
  {
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(BufferSource::FromDeclarations(
      Json::object(), "geometry.json", Mounts(), diagnostics));
    EXPECT_TRUE(
      HasDiagnosticCode(diagnostics, "buffer.container.buffers_missing"));
  }

  NOLINT_TEST_F(BufferSourceTest, RetainsDuplicateViewDiagnostic)
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
      declarations, "geometry.json", Mounts(), diagnostics));
    EXPECT_TRUE(HasDiagnosticCode(diagnostics, "buffer.view.name_duplicate"));
    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(diagnostics.front().object_path, "buffers[0].views[1].name");
  }

  NOLINT_TEST_F(BufferSourceTest, ResolvesAMountPathSource)
  {
    const auto declarations = Json::parse(R"([{
      "source":"/Content/Images/vertices.bin",
      "virtual_path":"/Content/Buffers/vertices.obuf"
    }])");
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto sources = BufferSource::FromDeclarations(
      declarations, "authored/geometry.json", Mounts(), diagnostics);
    ASSERT_HAS_VALUE(sources) << "Expected sources to contain a value";
    EXPECT_TRUE(diagnostics.empty());
    ASSERT_EQ(sources->size(), 1U);
    EXPECT_EQ(sources->front().source_path,
      std::filesystem::weakly_canonical(TempDir() / "Images/vertices.bin"));
  }

  NOLINT_TEST_F(BufferSourceTest, RefusesARelativeSourceLeavingItsMount)
  {
    const auto declarations = Json::parse(R"([{
      "source":"../Other/vertices.bin",
      "virtual_path":"/Content/Buffers/vertices.obuf"
    }])");
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto mounts = ContentAndOtherMountTable(TempDir());
    EXPECT_FALSE(BufferSource::FromDeclarations(
      declarations, "Content/geometry.json", *mounts, diagnostics));
    EXPECT_TRUE(
      HasDiagnosticCode(diagnostics, "import.reference_outside_mount"))
      << DiagnosticSummary(diagnostics);
  }
} // namespace
} // namespace oxygen::content::import::test
