//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/PakBuilder.cpp

#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace pak = oxygen::content::pak;

NOLINT_TEST(PakBuilderTest, PublicTypeDefaultsMatchSpec)
{
  using pak::PakBuildOptions;

  const PakBuildOptions options {};
  EXPECT_TRUE(options.deterministic);
  EXPECT_FALSE(options.embed_browse_index);
  EXPECT_FALSE(options.emit_manifest_in_full);
  EXPECT_TRUE(options.compute_crc32);
  EXPECT_FALSE(options.fail_on_warnings);
}

} // namespace
