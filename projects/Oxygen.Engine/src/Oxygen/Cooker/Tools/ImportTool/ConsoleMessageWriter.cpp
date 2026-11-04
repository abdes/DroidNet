//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iostream>
#include <memory>
#include <mutex>
#include <ostream>
#include <stdio.h> // NOLINT(*-deprecated-headers): declares MSVC _fileno
#include <string>
#include <string_view>

#include <corecrt_io.h>

#ifdef _WIN32
#else
#  include <unistd.h>
#endif

#include <fmt/color.h>
#include <fmt/format.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/ConsoleMessageWriter.h>
#include <Oxygen/Cooker/Tools/ImportTool/GlobalOptions.h>
#include <Oxygen/Cooker/Tools/ImportTool/MessageWriter.h>

namespace oxygen::content::import::tool {

namespace {

  auto IsInteractiveStdout() noexcept -> bool
  {
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
  }

  //! Prints one complete line per message; color is the only styling, and only
  //! on a terminal.
  class ConsoleMessageWriter final : public IMessageWriter {
  public:
    OXYGEN_MAKE_NON_COPYABLE(ConsoleMessageWriter)
    OXYGEN_MAKE_NON_MOVABLE(ConsoleMessageWriter)

    ConsoleMessageWriter(
      const bool quiet, const bool color, const toolkit::ToolStreams streams)
      : quiet_(quiet)
      , color_(color)
      , out_(streams.out)
      , err_(streams.err)
    {
    }

    ~ConsoleMessageWriter() override = default;

    auto Error(const std::string_view message) -> bool override
    {
      return Print(*err_, fmt::fg(kErrorColor), kErrorGlyph, message);
    }

    auto Warning(const std::string_view message) -> bool override
    {
      return Print(*err_, fmt::fg(fmt::color::yellow), kWarningGlyph, message);
    }

    auto Info(const std::string_view message) -> bool override
    {
      return !quiet_ && Print(*out_, fmt::fg(fmt::color::white), {}, message);
    }

    auto Report(const std::string_view message) -> bool override
    {
      return !quiet_
        && Print(*out_, fmt::fg(fmt::color::cyan), kSuccessGlyph, message);
    }

    auto Progress(const std::string_view message) -> bool override
    {
      if (quiet_) {
        return false;
      }
      auto line = std::string(message);
      for (auto& ch : line) {
        if (ch == '\n' || ch == '\r') {
          ch = ' ';
        }
      }
      return Print(*out_, fmt::fg(fmt::color::white), {}, line);
    }

  private:
    auto Print(std::ostream& stream, const fmt::text_style style,
      const std::string_view glyph, const std::string_view message) -> bool
    {
      const auto line = glyph.empty() ? std::string(message)
                                      : fmt::format("{} {}", glyph, message);
      std::scoped_lock lock(mutex_);
      if (color_) {
        stream << fmt::format(style, "{}", line);
      } else {
        stream << line;
      }
      // A killed tool still leaves the lines it printed.
      stream << '\n' << std::flush;
      return true;
    }

    static constexpr auto kErrorColor = fmt::rgb(220, 38, 38);
    static constexpr std::string_view kErrorGlyph = "×";
    static constexpr std::string_view kWarningGlyph = "▲";
    static constexpr std::string_view kSuccessGlyph = "✓";

    std::mutex mutex_;
    const bool quiet_;
    const bool color_;
    observer_ptr<std::ostream> out_;
    observer_ptr<std::ostream> err_;
  };

  class MutedMessageWriter final : public IMessageWriter {
  public:
    explicit MutedMessageWriter(const toolkit::ToolStreams streams)
      : err_(streams.err)
    {
    }

    auto Error(std::string_view message) -> bool override
    {
      *err_ << message << "\n";
      return true;
    }
    auto Warning(std::string_view message) -> bool override
    {
      *err_ << message << "\n";
      return true;
    }
    auto Info(std::string_view /*message*/) -> bool override { return false; }
    auto Report(std::string_view /*message*/) -> bool override { return false; }
    auto Progress(std::string_view /*message*/) -> bool override
    {
      return false;
    }

  private:
    observer_ptr<std::ostream> err_;
  };

} // namespace

auto CreateMessageWriter(const GlobalOptions& global_options,
  const toolkit::ToolStreams streams) -> std::unique_ptr<IMessageWriter>
{
  if (global_options.no_tui) {
    // Color only suits the real terminal, so an injected stream is recognised
    // by identity with the process standard output.
    const auto color = !global_options.no_color
      && streams.out.get() == &std::cout && IsInteractiveStdout();
    return std::make_unique<ConsoleMessageWriter>(
      global_options.quiet, color, streams);
  }
  return std::make_unique<MutedMessageWriter>(streams);
}

} // namespace oxygen::content::import::tool
