//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/LooseCookedWriter.cpp

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Testing/GTest.h>

// NOLINTBEGIN(*-magic-numbers)

namespace oxygen::content::testing {

namespace {

  using oxygen::content::import::LooseCookedWriter;
  using oxygen::cooker::test::ScopedTempDir;
  using oxygen::data::loose_cooked::FileKind;

  //! A resource table written without the data file it requires.
  struct MissingPairCase final {
    const char* case_name;
    FileKind table_kind;
    const char* table_relpath;
  };

  class LooseCookedWriterFilePairsTest
    : public ::testing::TestWithParam<MissingPairCase> { };

  NOLINT_TEST_P(LooseCookedWriterFilePairsTest, FinishThrowsWhenDataFileMissing)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path()
      / (std::string("loose_cooked_writer_pairs_") + GetParam().case_name);

    const std::vector<std::byte> bytes = {
      std::byte { 0x10 },
    };

    LooseCookedWriter writer(cooked_root);
    writer.WriteFile(GetParam().table_kind, GetParam().table_relpath, bytes);

    EXPECT_THROW(
      { [[maybe_unused]] const auto ignored = writer.Finish(); },
      std::runtime_error);
  }

  INSTANTIATE_TEST_SUITE_P(ResourceKinds, LooseCookedWriterFilePairsTest,
    ::testing::Values(
      MissingPairCase {
        .case_name = "Buffers",
        .table_kind = FileKind::kBuffersTable,
        .table_relpath = "Resources/buffers.table",
      },
      MissingPairCase {
        .case_name = "Textures",
        .table_kind = FileKind::kTexturesTable,
        .table_relpath = "Resources/textures.table",
      },
      MissingPairCase {
        .case_name = "Physics",
        .table_kind = FileKind::kPhysicsTable,
        .table_relpath = "Physics/Resources/physics.table",
      }),
    [](const ::testing::TestParamInfo<MissingPairCase>& info) -> std::string {
      return std::string(info.param.case_name);
    });

} // namespace

} // namespace oxygen::content::testing

// NOLINTEND(*-magic-numbers)
