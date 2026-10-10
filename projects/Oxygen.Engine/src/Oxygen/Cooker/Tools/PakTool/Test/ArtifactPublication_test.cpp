//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/PakTool/ArtifactPublication.cpp

#include <cstdlib>
#include <filesystem>
#include <string>

#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Cooker/Tools/PakTool/ArtifactPublication.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::pak::tool::MakeArtifactPublicationPlan;

class PakToolArtifactPublicationTest
  : public oxygen::cooker::test::TempDirTest {
protected:
};

NOLINT_TEST_F(PakToolArtifactPublicationTest,
  MakeArtifactPublicationPlanUsesDeterministicSiblingPaths)
{
  const auto plan
    = MakeArtifactPublicationPlan(TempDir() / "release" / "game.pak",
      TempDir() / "release" / "game.catalog.json",
      TempDir() / "release" / "game.manifest.json",
      TempDir() / "release" / "game.report.json");

  EXPECT_EQ(plan.pak.staged_path, TempDir() / "release" / "game.pak.staged");
  EXPECT_EQ(plan.pak.backup_path, TempDir() / "release" / "game.pak.previous");
  ASSERT_TRUE(plan.manifest.has_value());
  EXPECT_EQ(plan.manifest->staged_path,
    TempDir() / "release" / "game.manifest.json.staged");
  ASSERT_TRUE(plan.report.has_value());
  EXPECT_EQ(plan.report->backup_path,
    TempDir() / "release" / "game.report.json.previous");
}

} // namespace
