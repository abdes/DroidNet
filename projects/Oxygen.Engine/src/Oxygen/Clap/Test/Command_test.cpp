//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause).
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>

using testing::Eq;
using testing::IsTrue;

namespace asap::clap {

namespace {

  // NOLINTNEXTLINE
  TEST(Command, Default)
  {
    //! [Default command]
    std::unique_ptr<Command> cmd = CommandBuilder(Command::DEFAULT);
    //! [Default command]
    EXPECT_THAT(cmd->Path().size(), Eq(1));
    EXPECT_THAT(cmd->Path(), testing::Contains(""));
    EXPECT_THAT(cmd->IsDefault(), IsTrue());
  }

  // NOLINTNEXTLINE
  TEST(Command, OneSegmentPath)
  {
    //! [Non-default command path]
    std::unique_ptr<Command> cmd = CommandBuilder("path");
    //! [Non-default command path]
    EXPECT_THAT(cmd->Path().size(), Eq(1));
    EXPECT_THAT(cmd->Path(), testing::Contains("path"));
  }

  // NOLINTNEXTLINE
  TEST(Command, MultiSegmentPath)
  {
    std::unique_ptr<Command> cmd = CommandBuilder("segment1", "segment2");
    EXPECT_THAT(cmd->Path().size(), Eq(2));
    EXPECT_THAT(cmd->Path(), testing::Contains("segment1"));
    EXPECT_THAT(cmd->Path(), testing::Contains("segment2"));
  }

  // NOLINTNEXTLINE
  TEST(Command, DefaultFollowedByOtherSegmentIsIllegalPath)
  {
    // NOLINTNEXTLINE(hicpp-avoid-goto, cppcoreguidelines-avoid-goto)
    ASSERT_THROW(CommandBuilder("", "segment"), std::exception);
  }

  // NOLINTNEXTLINE
  TEST(Command, MultipleSegmentsContainingDefaultIsIllegalPath)
  {
    // NOLINTNEXTLINE(hicpp-avoid-goto, cppcoreguidelines-avoid-goto)
    ASSERT_THROW(CommandBuilder("sgement1", "", "segment2"), std::exception);
  }

} // namespace

} // namespace asap::clap
