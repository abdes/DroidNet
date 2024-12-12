//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "oxygen/core/version.h"

#include "gtest/gtest.h"
#include "oxygen/version-info.h"

// NOLINTNEXTLINE
TEST(VersionTests, Major) {
  EXPECT_EQ(oxygen::version::Major(), oxygen::info::cVersionMajor);
}

// NOLINTNEXTLINE
TEST(VersionTests, Minor) {
  EXPECT_EQ(oxygen::version::Minor(), oxygen::info::cVersionMinor);
}

// NOLINTNEXTLINE
TEST(VersionTests, Patch) {
  EXPECT_EQ(oxygen::version::Patch(), oxygen::info::cVersionPatch);
}

// NOLINTNEXTLINE
TEST(VersionTests, Version) {
  const std::string expectedVersion = std::to_string(oxygen::version::Major())
      + "." + std::to_string(oxygen::version::Minor()) + "."
      + std::to_string(oxygen::version::Patch());
  EXPECT_EQ(oxygen::version::Version(), expectedVersion);
}

// NOLINTNEXTLINE
TEST(VersionTests, VersionFull) {
  const std::string version = oxygen::version::Version();
  const std::string versionFull = oxygen::version::VersionFull();
  EXPECT_TRUE(versionFull.find(version) != std::string::npos);
}

// NOLINTNEXTLINE
TEST(VersionTests, NameVersion) {
  const std::string version = oxygen::version::Version();
  const std::string nameVersion = oxygen::version::NameVersion();
  EXPECT_TRUE(nameVersion.find("Oxygen") != std::string::npos);
  EXPECT_TRUE(nameVersion.find(version) != std::string::npos);
}
