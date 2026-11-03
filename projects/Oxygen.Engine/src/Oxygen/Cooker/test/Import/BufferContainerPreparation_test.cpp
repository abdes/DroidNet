//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/BufferSource.cpp (FromContainer)

#include <algorithm>
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
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace oxygen::content::import::test {
namespace {
  using internal::BufferSource;

  using oxygen::cooker::test::BufferContainer;
  using oxygen::cooker::test::ContentMountTable;
  using oxygen::cooker::test::DiagnosticSummary;
  using oxygen::cooker::test::HasDiagnosticCode;
  using oxygen::testing::TempDirTest;

  constexpr auto kContainer
    = std::string_view { "/Content/Geometry/geometry.buffers" };

  //! The project's `Content` mount is the temporary root.
  class BufferContainerPreparationTest : public TempDirTest {
  protected:
    auto Mounts() -> const MountTable&
    {
      if (!mounts_) {
        mounts_ = ContentMountTable(TempDir());
      }
      return *mounts_;
    }

    //! Prepares `text` as the container, with the diagnostics it adds.
    auto Prepare(
      const std::string_view text, std::vector<ImportDiagnostic>& diagnostics)
    {
      return BufferSource::FromContainer(
        text, "geometry.buffers.json", kContainer, Mounts(), diagnostics);
    }

  private:
    std::shared_ptr<const MountTable> mounts_;
  };

  NOLINT_TEST_F(BufferContainerPreparationTest, AcceptsAValidContainer)
  {
    auto container = BufferContainer { std::string(kContainer) };
    container.AppendBuffer(
      {
        { "source", "vertices.bin" },
        { "usage_flags", 3 },
        { "element_stride", 16 },
      },
      "/Content/Geometry/vertices.obuf");
    auto diagnostics = std::vector<ImportDiagnostic> {};

    const auto sources = Prepare(container.Text(), diagnostics);

    ASSERT_HAS_VALUE(sources) << DiagnosticSummary(diagnostics);
    EXPECT_TRUE(diagnostics.empty());
    ASSERT_EQ(sources->size(), 1U);
    EXPECT_EQ(sources->front().source_id, "/Content/Geometry/vertices.obuf");
    EXPECT_EQ(sources->front().json_pointer, "/buffers/0");
    EXPECT_EQ(sources->front().element_stride, 16U);
  }

  NOLINT_TEST_F(BufferContainerPreparationTest, RefusesInvalidJson)
  {
    auto diagnostics = std::vector<ImportDiagnostic> {};

    EXPECT_FALSE(Prepare("{ \"buffers\": [ ", diagnostics));

    ASSERT_EQ(diagnostics.size(), 1U) << DiagnosticSummary(diagnostics);
    EXPECT_EQ(
      diagnostics.front().code, "buffer.container.request_invalid_json");
    EXPECT_EQ(diagnostics.front().source_path, "geometry.buffers.json");
    EXPECT_TRUE(diagnostics.front().json_pointer.empty());
  }

  NOLINT_TEST_F(BufferContainerPreparationTest, RefusesComments)
  {
    auto diagnostics = std::vector<ImportDiagnostic> {};

    EXPECT_FALSE(Prepare("// note\n{ \"buffers\": [] }", diagnostics));

    EXPECT_TRUE(
      HasDiagnosticCode(diagnostics, "buffer.container.request_invalid_json"));
  }

  NOLINT_TEST_F(BufferContainerPreparationTest, RefusesANonObjectRoot)
  {
    auto diagnostics = std::vector<ImportDiagnostic> {};

    EXPECT_FALSE(Prepare("[]", diagnostics));

    ASSERT_EQ(diagnostics.size(), 1U) << DiagnosticSummary(diagnostics);
    EXPECT_EQ(
      diagnostics.front().code, "buffer.container.request_invalid_json");
    EXPECT_TRUE(diagnostics.front().json_pointer.empty());
  }

  NOLINT_TEST_F(BufferContainerPreparationTest, RefusesAMissingBuffersArray)
  {
    auto diagnostics = std::vector<ImportDiagnostic> {};

    EXPECT_FALSE(Prepare("{}", diagnostics));

    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(
      diagnostics.front().code, "buffer.container.schema_validation_failed");
    EXPECT_TRUE(diagnostics.front().json_pointer.empty());
  }

  NOLINT_TEST_F(BufferContainerPreparationTest, LocatesASchemaIssueInAChunk)
  {
    auto container = BufferContainer { std::string(kContainer) };
    container.AppendBuffer(
      { { "source", "vertices.bin" } }, "/Content/Geometry/vertices.obuf");
    container.Set("/buffers/0/element_stride", "sixteen");
    auto diagnostics = std::vector<ImportDiagnostic> {};

    EXPECT_FALSE(Prepare(container.Text(), diagnostics));

    ASSERT_FALSE(diagnostics.empty());
    EXPECT_TRUE(std::ranges::any_of(diagnostics,
      [](const ImportDiagnostic& diagnostic) -> bool {
        return diagnostic.code == "buffer.container.schema_validation_failed"
          && diagnostic.json_pointer == "/buffers/0/element_stride";
      }))
      << DiagnosticSummary(diagnostics);
  }

  //! The document schema already covers every chunk, so a refused document is
  //! not prepared again chunk by chunk.
  NOLINT_TEST_F(BufferContainerPreparationTest, ReportsEachSchemaIssueOnce)
  {
    auto container = BufferContainer { std::string(kContainer) };
    container.AppendBuffer(
      { { "source", "vertices.bin" } }, "/Content/Geometry/vertices.obuf");
    container.Set("/buffers/0/element_stride", "sixteen");
    auto diagnostics = std::vector<ImportDiagnostic> {};

    EXPECT_FALSE(Prepare(container.Text(), diagnostics));

    auto pointers = std::vector<std::string> {};
    for (const auto& diagnostic : diagnostics) {
      EXPECT_EQ(diagnostic.code, "buffer.container.schema_validation_failed");
      pointers.push_back(diagnostic.json_pointer + diagnostic.message);
    }
    std::ranges::sort(pointers);
    EXPECT_EQ(std::ranges::adjacent_find(pointers), pointers.end())
      << DiagnosticSummary(diagnostics);
  }

  NOLINT_TEST_F(BufferContainerPreparationTest, RefusesDuplicateProducts)
  {
    auto container = BufferContainer { std::string(kContainer) };
    container
      .AppendBuffer({ { "source", "a.bin" } }, "/Content/Geometry/shared.obuf")
      .AppendBuffer({ { "source", "b.bin" } }, "/Content/Geometry/shared.obuf");
    auto diagnostics = std::vector<ImportDiagnostic> {};

    EXPECT_FALSE(Prepare(container.Text(), diagnostics));

    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(
      diagnostics.front().code, "buffer.container.virtual_path_duplicate");
    EXPECT_EQ(diagnostics.front().json_pointer, "/buffers/1/virtual_path");
  }
} // namespace
} // namespace oxygen::content::import::test
