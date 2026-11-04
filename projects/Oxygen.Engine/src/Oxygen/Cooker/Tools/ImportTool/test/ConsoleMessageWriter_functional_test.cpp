//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/ImportTool/ConsoleMessageWriter.cpp

#include <ostream>
#include <sstream>
#include <string>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/ConsoleMessageWriter.h>
#include <Oxygen/Cooker/Tools/ImportTool/GlobalOptions.h>
#include <Oxygen/Cooker/Tools/ImportTool/MessageWriter.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace tool = oxygen::content::import::tool;
using oxygen::content::toolkit::ToolStreams;

class ConsoleMessageWriterTest : public ::testing::Test {
protected:
  //! Prints one message of every kind through the --no-tui writer.
  auto PrintAll(const bool quiet) -> void
  {
    auto options = tool::GlobalOptions {};
    options.no_tui = true;
    options.quiet = quiet;
    const auto writer = tool::CreateMessageWriter(options,
      ToolStreams {
        .out = oxygen::observer_ptr<std::ostream> { &out_ },
        .err = oxygen::observer_ptr<std::ostream> { &err_ },
      });
    writer->Info("info");
    writer->Report("report");
    writer->Progress("progress\rline");
    writer->Warning("warning");
    writer->Error("error");
  }

  [[nodiscard]] auto Out() const -> std::string { return out_.str(); }
  [[nodiscard]] auto Err() const -> std::string { return err_.str(); }

private:
  std::ostringstream out_;
  std::ostringstream err_;
};

//! --no-tui prints whole lines, with nothing that only a terminal understands.
NOLINT_TEST_F(ConsoleMessageWriterTest, NoTuiPrintsPlainLines)
{
  PrintAll(false);

  EXPECT_EQ(Out(), "info\n✓ report\nprogress line\n");
  EXPECT_EQ(Err(), "▲ warning\n× error\n");
}

//! -q leaves failures only.
NOLINT_TEST_F(ConsoleMessageWriterTest, QuietPrintsOnlyWarningsAndErrors)
{
  PrintAll(true);

  EXPECT_TRUE(Out().empty()) << Out();
  EXPECT_EQ(Err(), "▲ warning\n× error\n");
}

} // namespace
