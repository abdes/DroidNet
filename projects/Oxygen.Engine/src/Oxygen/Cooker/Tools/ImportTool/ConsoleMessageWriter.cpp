//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <atomic>
#include <cstddef>
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
#include <ftxui/screen/terminal.hpp>

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

  class ConsoleMessageWriter final : public IMessageWriter {
  public:
    OXYGEN_MAKE_NON_COPYABLE(ConsoleMessageWriter)
    OXYGEN_MAKE_NON_MOVABLE(ConsoleMessageWriter)

    ConsoleMessageWriter(
      bool quiet, bool no_color, bool interactive, toolkit::ToolStreams streams)
      : quiet_(quiet)
      , interactive_(interactive)
      , no_color_(no_color || !interactive_)
      , out_(streams.out)
      , err_(streams.err)
    {
      if (interactive_) {
        *out_ << "\x1b[?25l" << std::flush; // hides the cursor
      }
    }

    ~ConsoleMessageWriter() override
    {
      if (interactive_) {
        // Restores the cursor; a destructor must not throw.
        try {
          *out_ << "\x1b[?25h" << std::flush;
        } catch (...) { // NOLINT(bugprone-empty-catch)
        }
      }
    }

    auto Error(std::string_view message) -> bool override
    {
      std::scoped_lock lock(mutex_);
      ClearProgressLine();
      if (no_color_) {
        *err_ << (interactive_ ? "\r" : "") << kErrorGlyph << ' ' << message
              << '\n';
      } else {
        *err_ << fmt::format(fmt::fg(kErrorColor), "\r{} {}\r\n", kErrorGlyph,
          std::string(message));
      }
      last_was_progress_ = false;
      return true;
    }

    auto Warning(std::string_view message) -> bool override
    {
      std::scoped_lock lock(mutex_);
      ClearProgressLine();
      if (no_color_) {
        *err_ << (interactive_ ? "\r" : "") << kWarningGlyph << ' ' << message
              << '\n';
      } else {
        *err_ << fmt::format(fmt::fg(fmt::color::yellow), "\r{} {}\r\n",
          kWarningGlyph, std::string(message));
      }
      last_was_progress_ = false;
      return true;
    }

    auto Info(std::string_view message) -> bool override
    {
      if (quiet_) {
        return false;
      }
      std::scoped_lock lock(mutex_);
      ClearProgressLine();
      if (no_color_) {
        *out_ << (interactive_ ? "\r" : "") << message << '\n';
      } else {
        *out_ << fmt::format(
          fmt::fg(fmt::color::white), "\r{}\r\n", std::string(message));
      }
      last_was_progress_ = false;
      return true;
    }

    auto Report(std::string_view message) -> bool override
    {
      std::scoped_lock lock(mutex_);
      ClearProgressLine();
      if (no_color_) {
        *out_ << (interactive_ ? "\r" : "") << kSuccessGlyph << ' ' << message
              << '\n';
      } else {
        *out_ << fmt::format(fmt::fg(fmt::color::cyan), "\r{} {}\r\n",
          kSuccessGlyph, std::string(message));
      }
      last_was_progress_ = false;
      return true;
    }

    auto Progress(std::string_view message) -> bool override
    {
      if (quiet_) {
        return false;
      }
      std::scoped_lock lock(mutex_);
      const auto safe_message = SanitizeProgressMessage(message);
      if (!interactive_) {
        *out_ << safe_message << '\n';
        out_->flush();
        return true;
      }
      const auto frame = NextSpinnerFrame();
      const auto text = fmt::format("{} {}", frame, safe_message);
      const auto width = GetTerminalWidth();
      std::string line = FitToWidth(text, width);
      if (no_color_) {
        *out_ << fmt::format("\r{}", line);
      } else {
        *out_ << fmt::format(
          fmt::emphasis::faint | fmt::fg(fmt::color::white), "\r{}", line);
      }
      out_->flush();
      last_progress_len_ = line.size();
      last_was_progress_ = true;
      return true;
    }

  private:
    static constexpr size_t kProgressMaxWidth = 80U;

    static auto FitToWidth(const std::string_view text, const size_t width)
      -> std::string
    {
      const auto max_width = width > 0U ? width : kProgressMaxWidth;
      if (text.size() >= max_width) {
        std::string result(text.substr(0, max_width));
        if (max_width > 3U) {
          result.replace(max_width - 3U, 3U, "...");
        }
        return result;
      }
      std::string result(text);
      result.append(max_width - result.size(), ' ');
      return result;
    }

    static auto GetTerminalWidth() -> size_t
    {
      const auto size = ftxui::Terminal::Size();
      if (size.dimx <= 0) {
        return kProgressMaxWidth;
      }
      return static_cast<size_t>(size.dimx);
    }

    static auto SanitizeProgressMessage(const std::string_view message)
      -> std::string
    {
      std::string sanitized(message);
      for (auto& ch : sanitized) {
        if (ch == '\n' || ch == '\r') {
          ch = ' ';
        }
      }
      return sanitized;
    }

    auto ClearProgressLine() const -> void
    {
      if (last_was_progress_) {
        *out_ << '\r';
        if (last_progress_len_ > 0U) {
          *out_ << std::string(last_progress_len_, ' ') << '\r';
        }
        out_->flush();
      }
    }

    static auto NextSpinnerFrame() -> std::string_view
    {
      constexpr std::array<std::string_view, 4> kFrames {
        "⠋",
        "⠙",
        "⠹",
        "⠸",
      };
      const auto index = spinner_index_++ % kFrames.size();
      return kFrames.at(index);
    }

    static constexpr auto kErrorColor = fmt::rgb(220, 38, 38);
    static constexpr std::string_view kErrorGlyph = "×";
    static constexpr std::string_view kWarningGlyph = "▲";
    static constexpr std::string_view kSuccessGlyph = "✓";

    std::mutex mutex_;
    const bool quiet_;
    const bool interactive_;
    const bool no_color_;
    observer_ptr<std::ostream> out_;
    observer_ptr<std::ostream> err_;
    bool last_was_progress_ = false;
    size_t last_progress_len_ = 0U;
    static inline std::atomic<size_t> spinner_index_ { 0U };
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
    // Cursor control and animation only suit the real terminal, so an injected
    // stream is recognised by identity with the process standard output.
    const auto interactive
      = streams.out.get() == &std::cout && IsInteractiveStdout();
    return std::make_unique<ConsoleMessageWriter>(
      global_options.quiet, global_options.no_color, interactive, streams);
  }
  return std::make_unique<MutedMessageWriter>(streams);
}

} // namespace oxygen::content::import::tool
