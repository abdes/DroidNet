//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/BufferSource.cpp

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/BufferSource.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/TempDir.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {
  using internal::BufferSource;
  using Json = nlohmann::basic_json<>;

  using oxygen::cooker::test::BufferContainer;
  using oxygen::cooker::test::ContentAndOtherMountTable;
  using oxygen::cooker::test::ContentMountTable;
  using oxygen::cooker::test::DiagnosticSummary;
  using oxygen::cooker::test::HasDiagnosticCode;
  using oxygen::cooker::test::TempDirTest;

  constexpr auto kContainer
    = std::string_view { "/Content/Geometry/geometry.buffers" };

  //! The buffer declarations of a geometry descriptor, as the source reads
  //! them.
  auto DeclarationsOf(const BufferContainer& container) -> Json
  {
    return container.Json().at("buffers");
  }

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
    auto container = BufferContainer { "/Content/Geometry/geometry.buffers" };
    container.AppendBuffer(Json::parse(R"({
      "source":"vertices.bin",
      "usage_flags":1,"element_stride":16,"alignment":32,
      "views":[{"name":"triangle","element_offset":0,"element_count":3}]
    })"),
      "/OtherMount/Buffers/vertices.obuf");
    const auto declarations = DeclarationsOf(container);
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto sources = BufferSource::FromDeclarations(declarations,
      "authored/geometry.json", container.Identity(), Mounts(), diagnostics);
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
    auto container = BufferContainer { "/Content/Geometry/geometry.buffers" };
    container
      .AppendBuffer({ { "source", "a.bin" } }, "/Content/Buffers/shared.obuf")
      .AppendBuffer({ { "source", "b.bin" } }, "/Content/Buffers/shared.obuf");
    const auto declarations = DeclarationsOf(container);
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(BufferSource::FromDeclarations(declarations, "geometry.json",
      container.Identity(), Mounts(), diagnostics));
    EXPECT_TRUE(HasDiagnosticCode(
      diagnostics, "buffer.container.virtual_path_duplicate"));
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.front().object_path, "buffers[1].virtual_path");
  }

  NOLINT_TEST_F(BufferSourceTest, NamesProductsRelativeToTheContainer)
  {
    auto container = BufferContainer { std::string(kContainer) };
    container
      .AppendBuffer({ { "source", "a.bin" } }, "/Content/Geometry/a.obuf")
      .AppendBuffer({ { "source", "b.bin" } }, "/Content/Buffers/b.obuf");
    const auto declarations = DeclarationsOf(container);
    EXPECT_EQ(declarations.at(0).at("virtual_path"), "a.obuf");
    EXPECT_EQ(declarations.at(1).at("virtual_path"), "../Buffers/b.obuf");
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto sources = BufferSource::FromDeclarations(declarations,
      "geometry.json", container.Identity(), Mounts(), diagnostics);
    ASSERT_HAS_VALUE(sources) << DiagnosticSummary(diagnostics);
    ASSERT_EQ(sources->size(), 2U);
    EXPECT_EQ(sources->at(0).source_id, "/Content/Geometry/a.obuf");
    EXPECT_EQ(sources->at(1).source_id, "/Content/Buffers/b.obuf");
  }

  NOLINT_TEST_F(BufferSourceTest, RejectsTwoSpellingsOfOneProduct)
  {
    auto container = BufferContainer { std::string(kContainer) };
    container
      .AppendBuffer({ { "source", "a.bin" } }, "/Content/Geometry/a.obuf")
      .AppendBuffer({ { "source", "b.bin" } }, "/Content/Geometry/a.obuf");
    auto declarations = DeclarationsOf(container);
    declarations.at(1)["virtual_path"] = "./a.obuf";
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(BufferSource::FromDeclarations(declarations, "geometry.json",
      container.Identity(), Mounts(), diagnostics));
    EXPECT_TRUE(
      HasDiagnosticCode(diagnostics, "buffer.container.virtual_path_duplicate"))
      << DiagnosticSummary(diagnostics);
  }

  NOLINT_TEST_F(BufferSourceTest, RefusesAProductNamedAbsolutelyInItsOwnMount)
  {
    auto container = BufferContainer { std::string(kContainer) };
    container.AppendBuffer(
      { { "source", "a.bin" } }, "/Content/Buffers/a.obuf");
    auto declarations = DeclarationsOf(container);
    declarations.at(0)["virtual_path"] = "/Content/Buffers/a.obuf";
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(BufferSource::FromDeclarations(declarations, "geometry.json",
      container.Identity(), Mounts(), diagnostics));
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.front().code, "import.reference_invalid");
    EXPECT_THAT(diagnostics.front().message,
      testing::HasSubstr("vpath.reference_self_absolute"));
    EXPECT_EQ(diagnostics.front().object_path, "buffers[0].virtual_path");
  }

  NOLINT_TEST_F(BufferSourceTest, RefusesAProductClimbingAboveTheMount)
  {
    auto container = BufferContainer { std::string(kContainer) };
    container.AppendBuffer(
      { { "source", "a.bin" } }, "/Content/Buffers/a.obuf");
    auto declarations = DeclarationsOf(container);
    declarations.at(0)["virtual_path"] = "../../a.obuf";
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(BufferSource::FromDeclarations(declarations, "geometry.json",
      container.Identity(), Mounts(), diagnostics));
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.front().code, "import.reference_invalid");
    EXPECT_THAT(diagnostics.front().message,
      testing::HasSubstr("vpath.reference_outside_mount"));
  }

  NOLINT_TEST_F(BufferSourceTest, RejectsInvalidDeclarationWithSourceLocation)
  {
    auto container = BufferContainer { "/Content/Geometry/geometry.buffers" };
    container.AppendBuffer({ { "source", "a.bin" }, { "element_format", 256 } },
      "/Content/Buffers/a.obuf");
    const auto declarations = DeclarationsOf(container);
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(BufferSource::FromDeclarations(declarations, "geometry.json",
      container.Identity(), Mounts(), diagnostics, "lod.buffers"));
    EXPECT_TRUE(HasDiagnosticCode(
      diagnostics, "buffer.container.schema_validation_failed"));
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics.front().source_path, "geometry.json");
    EXPECT_TRUE(diagnostics.front().object_path.starts_with("lod.buffers[0]"));
  }

  NOLINT_TEST_F(BufferSourceTest, PreservesFormatDrivenStride)
  {
    auto container = BufferContainer { "/Content/Geometry/geometry.buffers" };
    container.AppendBuffer({ { "source", "a.bin" }, { "element_format", 1 } },
      "/Content/Buffers/a.obuf");
    const auto declarations = DeclarationsOf(container);
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto sources = BufferSource::FromDeclarations(declarations,
      "geometry.json", container.Identity(), Mounts(), diagnostics);
    ASSERT_HAS_VALUE(sources) << "Expected sources to contain a value";
    ASSERT_EQ(sources->size(), 1U);
    EXPECT_EQ(sources->front().element_stride, 0U);
    EXPECT_EQ(sources->front().element_format, 1U);
  }

  NOLINT_TEST_F(BufferSourceTest, ReportsWrongContainerType)
  {
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(BufferSource::FromDeclarations(
      Json::object(), "geometry.json", kContainer, Mounts(), diagnostics));
    EXPECT_TRUE(
      HasDiagnosticCode(diagnostics, "buffer.container.buffers_missing"));
  }

  NOLINT_TEST_F(BufferSourceTest, RetainsDuplicateViewDiagnostic)
  {
    auto container = BufferContainer { "/Content/Geometry/geometry.buffers" };
    container.AppendBuffer(Json::parse(R"({
      "source":"a.bin",
      "views":[
        {"name":"surface","element_offset":0,"element_count":3},
        {"name":"surface","element_offset":3,"element_count":3}
      ]
    })"),
      "/Content/Buffers/a.obuf");
    const auto declarations = DeclarationsOf(container);
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(BufferSource::FromDeclarations(declarations, "geometry.json",
      container.Identity(), Mounts(), diagnostics));
    EXPECT_TRUE(HasDiagnosticCode(diagnostics, "buffer.view.name_duplicate"));
    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(diagnostics.front().object_path, "buffers[0].views[1].name");
  }

  NOLINT_TEST_F(BufferSourceTest, ResolvesAMountPathSource)
  {
    auto container = BufferContainer { "/Content/Geometry/geometry.buffers" };
    container.AppendBuffer(
      { { "source", container.Spell("/Content/Images/vertices.bin") } },
      "/Content/Buffers/vertices.obuf");
    const auto declarations = DeclarationsOf(container);
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto sources = BufferSource::FromDeclarations(declarations,
      "authored/geometry.json", container.Identity(), Mounts(), diagnostics);
    ASSERT_HAS_VALUE(sources) << "Expected sources to contain a value";
    EXPECT_TRUE(diagnostics.empty());
    ASSERT_EQ(sources->size(), 1U);
    EXPECT_EQ(sources->front().source_path,
      std::filesystem::weakly_canonical(TempDir() / "Images/vertices.bin"));
  }

  NOLINT_TEST_F(BufferSourceTest, RefusesARelativeSourceLeavingItsMount)
  {
    auto container = BufferContainer { "/Content/Geometry/geometry.buffers" };
    container.AppendBuffer({ { "source", "../Other/vertices.bin" } },
      "/Content/Buffers/vertices.obuf");
    const auto declarations = DeclarationsOf(container);
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto mounts = ContentAndOtherMountTable(TempDir());
    EXPECT_FALSE(BufferSource::FromDeclarations(declarations,
      "Content/geometry.json", container.Identity(), *mounts, diagnostics));
    ASSERT_TRUE(HasDiagnosticCode(diagnostics, "import.reference_invalid"))
      << DiagnosticSummary(diagnostics);
    EXPECT_NE(diagnostics.front().message.find("vpath.reference_outside_mount"),
      std::string::npos);
  }
} // namespace
} // namespace oxygen::content::import::test
