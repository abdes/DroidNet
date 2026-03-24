//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/ShaderManager.h>

namespace {

using oxygen::graphics::ShaderManager;

NOLINT_TEST(ShaderManagerTest, MissingArchiveLeavesCacheEmpty)
{
  const auto archive_dir = std::filesystem::temp_directory_path()
    / "oxygen-shader-manager-missing-archive";
  std::filesystem::remove_all(archive_dir);

  ShaderManager manager({
    .backend_name = "test",
    .archive_file_name = "missing-shaders.bin",
    .archive_dir = archive_dir,
  });

  EXPECT_EQ(manager.GetShaderCount(), 0u);

  std::filesystem::remove_all(archive_dir);
}

} // namespace
