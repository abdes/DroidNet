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

#include "LooseCookedWriterTestSupport.h"

#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::testing {
namespace fixtures = oxygen::cooker::testing;

namespace {

  //! A material descriptor's file name.
  auto MaterialFile(const std::string_view name) -> std::string
  {
    return std::string(name) + ".omat";
  }
  using oxygen::content::import::LooseCookedWriter;
  using oxygen::cooker::testing::MemoryFileSystem;
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
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / suffix;

    const auto key = MakeFirstByteAssetKey(0x44);

    const auto bytes = fixtures::MaterialBuilder { "bytes" }.Bytes();
    const auto valid_virtual_path
      = std::string("/Cooked/Materials/") + MaterialFile("A");
    const auto valid_relpath = "Materials/" + MaterialFile("A");

    LooseCookedWriter writer(cooked_root, storage);

    switch (GetParam().target) {
    case PathTarget::kVirtualPath:
      EXPECT_THROW(writer.WriteAssetDescriptor(key, AssetType::kMaterial,
                     std::string_view(GetParam().path), valid_relpath, bytes,
                     {}, FromSource("/Cooked/Materials/A.omat.json")),
        std::runtime_error);
      break;
    case PathTarget::kDescriptorRelPath:
      EXPECT_THROW(writer.WriteAssetDescriptor(key, AssetType::kMaterial,
                     valid_virtual_path, GetParam().path, bytes, {},
                     FromSource("/Cooked/Materials/A.omat.json")),
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
        .path = std::string(".cooked/") + MaterialFile("A"),
      },
      BadPathCase {
        .case_name = "DotSegments",
        .target = PathTarget::kVirtualPath,
        .path = std::string("/Cooked/../") + MaterialFile("A"),
      },
      BadPathCase {
        .case_name = "Backslashes",
        .target = PathTarget::kVirtualPath,
        .path = std::string("\\\\.cooked\\\\") + MaterialFile("A"),
      },
      BadPathCase {
        .case_name = "DoubleSlash",
        .target = PathTarget::kVirtualPath,
        .path = std::string("/Cooked//") + MaterialFile("A"),
      },
      BadPathCase {
        .case_name = "TrailingSlash",
        .target = PathTarget::kVirtualPath,
        .path = std::string("/Cooked/") + MaterialFile("A") + "/",
      },
      BadPathCase {
        .case_name = "AbsoluteDescriptorRelPath",
        .target = PathTarget::kDescriptorRelPath,
        .path = "/Materials/" + MaterialFile("A"),
      },
      BadPathCase {
        .case_name = "DotDotDescriptorRelPath",
        .target = PathTarget::kDescriptorRelPath,
        .path = "Materials/../materials/" + MaterialFile("A"),
      },
      BadPathCase {
        .case_name = "ColonDescriptorRelPath",
        .target = PathTarget::kDescriptorRelPath,
        .path = "C:/Materials/" + MaterialFile("A"),
      },
      BadPathCase {
        .case_name = "BackslashFileRelPath",
        .target = PathTarget::kFileRelPath,
        .path = "Resources\\buffers.table",
      }),
    [](const ::testing::TestParamInfo<BadPathCase>& info) -> std::string {
      return std::string(info.param.case_name);
    });

} // namespace

} // namespace oxygen::content::testing
