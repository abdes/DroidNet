//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Platforms.h>
#include <Oxygen/Testing/GTest.h>

namespace {
using oxygen::base::PathIdentityKey;
using oxygen::base::ToLogicalPath;
using oxygen::base::ToNativePath;

NOLINT_TEST(FilesystemPathTest, EmptyPathDoesNotBecomeCurrentDirectory)
{
  EXPECT_TRUE(ToNativePath({}).empty());
  EXPECT_TRUE(ToLogicalPath({}).empty());
  EXPECT_TRUE(PathIdentityKey({}).empty());
}

NOLINT_TEST(FilesystemPathTest, IdentityKeysNormalizeRelativeComponents)
{
  EXPECT_EQ(PathIdentityKey("assets/../Cooked"),
    PathIdentityKey(std::filesystem::absolute("Cooked")));
  EXPECT_EQ(PathIdentityKey("Cooked/"), PathIdentityKey("Cooked"));
  const auto root = std::filesystem::absolute("Cooked").root_path();
  EXPECT_EQ(PathIdentityKey(root / "."), PathIdentityKey(root));
}

#ifdef OXYGEN_WINDOWS
NOLINT_TEST(FilesystemPathTest, IdentityKeysFoldAbsentUnicodePathsAndNamespaces)
{
  EXPECT_EQ(PathIdentityKey(L"C:/OxygenAbsent/\u00c4ssets/Cooked"),
    PathIdentityKey(LR"(\\?\C:\oxygenabsent\ässets\cooked)"));
}
NOLINT_TEST(FilesystemPathTest, DrivePathsAreAbsoluteAndNormalized)
{
  EXPECT_EQ(ToNativePath(L"C:/assets/old/../model.bin"),
    std::filesystem::path(LR"(\\?\C:\assets\model.bin)"));
  const auto relative = std::filesystem::path("assets/../model.bin");
  EXPECT_EQ(ToLogicalPath(ToNativePath(relative)),
    std::filesystem::absolute(relative).lexically_normal());
}

NOLINT_TEST(FilesystemPathTest, UncPathsRoundTripWithoutNetworkAccess)
{
  const auto logical = std::filesystem::path(LR"(\\server\share\model.bin)");
  EXPECT_EQ(ToNativePath(logical),
    std::filesystem::path(LR"(\\?\UNC\server\share\model.bin)"));
  EXPECT_EQ(ToLogicalPath(ToNativePath(logical)), logical);
  EXPECT_EQ(ToLogicalPath(LR"(\\?\uNc\server\share\model.bin)"), logical);
}

NOLINT_TEST(FilesystemPathTest, ExtendedAndDeviceNamespacesArePreserved)
{
  const auto extended = std::filesystem::path(LR"(\\?\C:\assets\model.bin)");
  const auto device = std::filesystem::path(LR"(\\.\pipe\oxygen)");
  EXPECT_EQ(ToNativePath(extended), extended);
  EXPECT_EQ(ToNativePath(device), device);
  EXPECT_EQ(ToLogicalPath(device), device);
  EXPECT_EQ(ToNativePath(ToLogicalPath(extended)), extended);
}
#else
NOLINT_TEST(FilesystemPathTest, IdentityKeysPreservePosixCase)
{
  EXPECT_NE(PathIdentityKey("Cooked"), PathIdentityKey("cooked"));
}
NOLINT_TEST(FilesystemPathTest, PosixPathsRetainTheirFilesystemSemantics)
{
  const auto path = std::filesystem::path("assets/link/../model.bin");
  EXPECT_EQ(ToNativePath(path), path);
  EXPECT_EQ(ToLogicalPath(path), path);
}
#endif
} // namespace
