//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Clap/Internal/Args.h>

using testing::Eq;
using testing::Ne;

using oxygen::clap::detail::Arguments;

namespace {

NOLINT_TEST(ConstructArguments, WithNonEmptyProgramName)
{
  constexpr auto argc = 1;
  std::array<const char*, 1> argv { { "bin/test-program" } };
  const Arguments cla { argc, argv.data() };
  EXPECT_THAT(cla.ProgramName(), Eq("bin/test-program"));
  EXPECT_THAT(cla.Args(), ::testing::IsEmpty());
}

NOLINT_TEST(ConstructArguments, WithManyArgs)
{
  constexpr auto argc = 4;
  std::array<const char*, 4> argv { { "test", "-x", "--opt=value", "arg" } };
  const Arguments cla { argc, argv.data() };
  EXPECT_THAT(cla.ProgramName(), Eq("test"));
  EXPECT_THAT(cla.Args(), ::testing::ElementsAre("-x", "--opt=value", "arg"));
}

} // namespace
