//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause).
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Clap/Internal/Args.h>

using testing::Eq;
using testing::Ne;

namespace asap::clap::detail {

namespace {

  // NOLINTNEXTLINE
  TEST(ConstructArguments, WithNonEmptyProgramName)
  {
    constexpr auto argc = 1;
    std::array<const char*, 1> argv { { "bin/test-program" } };
    const Arguments cla { argc, argv.data() };
    EXPECT_THAT(cla.ProgramName(), Eq("bin/test-program"));
    EXPECT_THAT(cla.Args().size(), Eq(0));
  }

  // NOLINTNEXTLINE
  TEST(ConstructArguments, WithManyArgs)
  {
    constexpr auto argc = 4;
    std::array<const char*, 4> argv { { "test", "-x", "--opt=value", "arg" } };
    const Arguments cla { argc, argv.data() };
    EXPECT_THAT(cla.ProgramName(), Eq("test"));
    EXPECT_THAT(cla.Args().size(), Eq(3));
    for (int index = 1; index < argc; index++) {
      EXPECT_THAT(
        std::find(cla.Args().begin(), cla.Args().end(), argv.at(index)),
        Ne(cla.Args().end()));
    }
  }

} // namespace

} // namespace asap::clap::detail
