//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <Oxygen/Clap/Internal/Args.h>
#include <Oxygen/Testing/GTest.h>

using testing::Eq;

using oxygen::clap::detail::Arguments;

namespace {

NOLINT_TEST(ConstructArguments, WithNonEmptyProgramName)
{
  std::array<const char*, 1> argv { { "bin/test-program" } };
  const Arguments cla { argv };
  EXPECT_THAT(cla.ProgramName(), Eq("bin/test-program"));
  EXPECT_THAT(cla.Args(), ::testing::IsEmpty());
}

NOLINT_TEST(ConstructArguments, WithManyArgs)
{
  std::array<const char*, 4> argv { { "test", "-x", "--opt=value", "arg" } };
  const Arguments cla { argv };
  EXPECT_THAT(cla.ProgramName(), Eq("test"));
  EXPECT_THAT(cla.Args(), ::testing::ElementsAre("-x", "--opt=value", "arg"));
}

} // namespace
