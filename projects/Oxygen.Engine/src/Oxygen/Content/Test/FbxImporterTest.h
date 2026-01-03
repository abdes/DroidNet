//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>
#include <system_error>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/LooseCookedLayout.h>

namespace oxygen::content::test {

//! Base harness for FBX importer tests.
/*!\
 Provides common helpers for working with temporary directories and writing
 minimal FBX fixtures.
*/
class FbxImporterTest : public ::testing::Test {
protected:
  [[nodiscard]] static auto MakeTempDir(std::string_view suffix)
    -> std::filesystem::path
  {
    const auto root
      = std::filesystem::temp_directory_path() / "oxygen_content_tests";
    const auto out_dir = root / std::filesystem::path(std::string(suffix));

    std::error_code ec;
    std::filesystem::remove_all(out_dir, ec);
    std::filesystem::create_directories(out_dir);

    return out_dir;
  }

  static auto WriteTextFile(
    const std::filesystem::path& path, const std::string_view contents) -> void
  {
    std::ofstream file(path.string(), std::ios::binary);
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  }

  static auto WriteBinaryFile(const std::filesystem::path& path,
    const std::span<const std::byte> bytes) -> void
  {
    std::ofstream file(path.string(), std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
  }
};

} // namespace oxygen::content::test
