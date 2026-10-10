//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/LooseCookedWriter.cpp

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "LooseCookedWriterTestSupport.h"

#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Cooker/Test/Support/DescriptorFixtures.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Testing/GTest.h>

// NOLINTBEGIN(*-magic-numbers)

namespace oxygen::content::testing {
namespace fixtures = oxygen::content::test;

namespace {

  using oxygen::content::import::LooseCookedLayout;
  using oxygen::content::import::LooseCookedWriter;
  using oxygen::cooker::test::ScopedTempDir;
  using oxygen::data::AssetType;
  using oxygen::data::loose_cooked::FileKind;

  //! Which argument of the writer carries the invalid path.
  enum class PathTarget : uint8_t {
    kVirtualPath,
    kDescriptorRelPath,
    kFileRelPath,
  };

  struct BadPathCase final {
    const char* case_name;
    PathTarget target;
    std::string path;
  };

  //! Parameterized fixture for invalid virtual and relative path strings.
  class LooseCookedWriterBadPathTest
    : public ::testing::TestWithParam<BadPathCase> { };

  NOLINT_TEST_P(LooseCookedWriterBadPathTest, RejectsInvalidPath)
  {
    const auto suffix
      = std::string("loose_cooked_writer_bad_path_") + GetParam().case_name;
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / suffix;

    const auto key = MakeFirstByteAssetKey(0x44);

    const auto bytes
      = fixtures::MaterialDescriptor("bytes", fixtures::MaterialVariant::kPlain)
          .bytes;
    const auto valid_virtual_path = std::string("/.cooked/Materials/")
      + LooseCookedLayout::MaterialDescriptorFileName("A");
    const auto valid_relpath
      = "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("A");

    LooseCookedWriter writer(cooked_root);

    switch (GetParam().target) {
    case PathTarget::kVirtualPath:
      EXPECT_THROW(
        writer.WriteAssetDescriptor(key, AssetType::kMaterial,
          std::string_view(GetParam().path), valid_relpath, bytes, {}),
        std::runtime_error);
      break;
    case PathTarget::kDescriptorRelPath:
      EXPECT_THROW(writer.WriteAssetDescriptor(key, AssetType::kMaterial,
                     valid_virtual_path, GetParam().path, bytes, {}),
        std::runtime_error);
      break;
    case PathTarget::kFileRelPath:
      EXPECT_THROW(
        writer.WriteFile(FileKind::kBuffersTable, GetParam().path, bytes),
        std::runtime_error);
      break;
    }
  }

  INSTANTIATE_TEST_SUITE_P(BadPaths, LooseCookedWriterBadPathTest,
    ::testing::Values(
      BadPathCase {
        .case_name = "MissingLeadingSlash",
        .target = PathTarget::kVirtualPath,
        .path = std::string(".cooked/")
          + LooseCookedLayout::MaterialDescriptorFileName("A"),
      },
      BadPathCase {
        .case_name = "DotSegments",
        .target = PathTarget::kVirtualPath,
        .path = std::string("/.cooked/../")
          + LooseCookedLayout::MaterialDescriptorFileName("A"),
      },
      BadPathCase {
        .case_name = "Backslashes",
        .target = PathTarget::kVirtualPath,
        .path = std::string("\\\\.cooked\\\\")
          + LooseCookedLayout::MaterialDescriptorFileName("A"),
      },
      BadPathCase {
        .case_name = "DoubleSlash",
        .target = PathTarget::kVirtualPath,
        .path = std::string("/.cooked//")
          + LooseCookedLayout::MaterialDescriptorFileName("A"),
      },
      BadPathCase {
        .case_name = "TrailingSlash",
        .target = PathTarget::kVirtualPath,
        .path = std::string("/.cooked/")
          + LooseCookedLayout::MaterialDescriptorFileName("A") + "/",
      },
      BadPathCase {
        .case_name = "AbsoluteDescriptorRelPath",
        .target = PathTarget::kDescriptorRelPath,
        .path
        = "/Materials/" + LooseCookedLayout::MaterialDescriptorFileName("A"),
      },
      BadPathCase {
        .case_name = "DotDotDescriptorRelPath",
        .target = PathTarget::kDescriptorRelPath,
        .path = "Materials/../materials/"
          + LooseCookedLayout::MaterialDescriptorFileName("A"),
      },
      BadPathCase {
        .case_name = "ColonDescriptorRelPath",
        .target = PathTarget::kDescriptorRelPath,
        .path
        = "C:/Materials/" + LooseCookedLayout::MaterialDescriptorFileName("A"),
      },
      BadPathCase {
        .case_name = "BackslashFileRelPath",
        .target = PathTarget::kFileRelPath,
        .path = "Resources\\buffers.table",
      }),
    [](const ::testing::TestParamInfo<BadPathCase>& info) -> std::string {
      return std::string(info.param.case_name);
    });

  NOLINT_TEST(LooseCookedWriterPathValidationTest,
    WriteAssetDescriptorDuplicateVirtualPathAcrossRunsThrows)
  {
    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "loose_cooked_writer_conflict_across_runs";

    const auto key0 = MakeFirstByteAssetKey(0x50);
    const auto key1 = MakeFirstByteAssetKey(0x51);

    const auto bytes
      = fixtures::MaterialDescriptor("bytes", fixtures::MaterialVariant::kPlain)
          .bytes;

    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteAssetDescriptor(key0, AssetType::kMaterial,
        "/.cooked/Materials/"
          + LooseCookedLayout::MaterialDescriptorFileName("A"),
        "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("A"),
        bytes, {});
      (void)writer.Finish();
    }

    LooseCookedWriter writer(cooked_root);
    try {
      writer.WriteAssetDescriptor(key1, AssetType::kMaterial,
        "/.cooked/Materials/"
          + LooseCookedLayout::MaterialDescriptorFileName("A"),
        "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("B"),
        bytes, {});
      FAIL() << "Expected virtual path collision.";
    } catch (const std::runtime_error& ex) {
      const std::string message = ex.what();
      EXPECT_THAT(
        message, ::testing::HasSubstr("Conflicting virtual path mapping"));
      EXPECT_THAT(message, ::testing::HasSubstr("WriteAssetDescriptor"));
      EXPECT_THAT(message,
        ::testing::HasSubstr("existing_descriptor='Materials/A.omat'"));
      EXPECT_THAT(message,
        ::testing::HasSubstr("incoming_descriptor='Materials/B.omat'"));
    }
  }

} // namespace

} // namespace oxygen::content::testing

// NOLINTEND(*-magic-numbers)
