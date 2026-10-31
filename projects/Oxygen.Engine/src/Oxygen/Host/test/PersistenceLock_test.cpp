//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <optional>

#include <Oxygen/Host/PersistenceLock.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {

using oxygen::host::PersistenceLock;

class PersistenceLockTest : public oxygen::testing::TempDirTest { };

//! The first claim owns the folder; another fails while the owner holds it,
//! and succeeds once the owner releases it.
NOLINT_TEST_F(PersistenceLockTest, OneOwnerAtATime)
{
  auto owner = std::optional<PersistenceLock> {};
  owner.emplace(TempDir());
  ASSERT_TRUE(owner->IsOwner());
  EXPECT_FALSE(PersistenceLock(TempDir()).IsOwner());

  owner.reset();
  EXPECT_TRUE(PersistenceLock(TempDir()).IsOwner());
}

//! The claim's lock file exists only while the claim is held.
NOLINT_TEST_F(PersistenceLockTest, ReleasingTheClaimRemovesItsFile)
{
  {
    const auto owner = PersistenceLock(TempDir());
    ASSERT_TRUE(owner.IsOwner());
    EXPECT_FALSE(std::filesystem::is_empty(TempDir()));
  }
  EXPECT_TRUE(std::filesystem::is_empty(TempDir()));
}

} // namespace
