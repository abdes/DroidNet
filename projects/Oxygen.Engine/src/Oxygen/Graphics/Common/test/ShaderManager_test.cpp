//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/ShaderManager.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace {

using oxygen::graphics::ShaderManager;

NOLINT_TEST(ShaderManagerTest, MissingArchiveLeavesCacheEmpty)
{
  const auto archive_dir
    = oxygen::testing::ScopedTempDir().Path() / "missing-archive";

  ShaderManager manager({
    .backend_name = "test",
    .archive_file_name = "missing-shaders.bin",
    .archive_dir = archive_dir,
  });

  EXPECT_EQ(manager.GetShaderCount(), 0U);
}

} // namespace
