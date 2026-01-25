//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <system_error>
#include <thread>
#include <vector>

#include <fmt/color.h>
#include <fmt/format.h>
#include <ftxui/screen/terminal.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Content/Import/AsyncImportService.h>
#include <Oxygen/Content/Tools/ImportTool/BatchCommand.h>
#include <Oxygen/Content/Tools/ImportTool/CliBuilder.h>
#include <Oxygen/Content/Tools/ImportTool/FbxCommand.h>
#include <Oxygen/Content/Tools/ImportTool/GlobalOptions.h>
#include <Oxygen/Content/Tools/ImportTool/GltfCommand.h>
#include <Oxygen/Content/Tools/ImportTool/ImportCommand.h>
#include <Oxygen/Content/Tools/ImportTool/MessageWriter.h>
#include <Oxygen/Content/Tools/ImportTool/TextureCommand.h>

// Create and inject concrete writers. The concrete implementations live
// only in main and are owned here; GlobalOptions holds non-owning
// observer_ptrs that point to these objects. Clients MUST NOT create or
// own writers.
using oxygen::content::import::tool::IMessageWriter;

// Local concrete implementations (not visible outside this translation
// unit).
namespace {
using oxygen::clap::Command;
using oxygen::clap::CommandLineContext;
using oxygen::content::import::AsyncImportService;
using oxygen::content::import::ImportConcurrency;
using oxygen::content::import::ImportPipelineConcurrency;
using oxygen::content::import::tool::BatchCommand;
using oxygen::content::import::tool::GlobalOptions;
using oxygen::content::import::tool::ImportCommand;

std::atomic<bool> g_stop_requested { false };
std::atomic<bool> g_stop_handled { false };
std::atomic<int> g_stop_signal { 0 };
std::atomic<int> g_stop_count { 0 };

auto HandleStopSignal(const int signal) -> void
{
  const int count = g_stop_count.fetch_add(1, std::memory_order_acq_rel) + 1;
  if (count == 1) {
    g_stop_requested.store(true, std::memory_order_relaxed);
    int expected = 0;
    g_stop_signal.compare_exchange_strong(
      expected, signal, std::memory_order_acq_rel);
    std::signal(SIGINT, HandleStopSignal);
    std::signal(SIGTERM, HandleStopSignal);
    return;
  }
  if (count >= 3) {
    const int exit_code = (signal == SIGTERM) ? 143 : 130;
    std::_Exit(exit_code);
  }
}

auto ExitStatusFromError(const std::error_code& error) -> int
{
  if (error == std::errc::invalid_argument) {
    return 1;
  }
  return 2;
}

auto IsMetaCommand(
  std::string_view command_path, const CommandLineContext& context) -> bool
{
  return command_path == Command::VERSION || command_path == Command::HELP
    || context.ovm.HasOption(Command::HELP);
}

auto FinalizeGlobalOptions(const CommandLineContext& context) -> void
{
  if (context.global_option_groups != nullptr) {
    for (const auto& group : *context.global_option_groups) {
      for (const auto& option : *group.first) {
        option->FinalizeValue(context.ovm);
      }
    }
  }
}

auto ApplyLoggingOptions(const GlobalOptions& options) -> void
{
  // Control colored output to stderr according to --no-color
  loguru::g_colorlogtostderr = !options.no_color;

  // When running with an interactive TUI we want to avoid interleaving
  // log messages on stderr with the curses-style UI. Disable stderr logging
  // completely when the TUI is enabled (i.e., --no-tui is not set).
  if (!options.no_tui) {
    loguru::g_log_to_stderr = false;
  }
}

auto Trim(std::string_view value) -> std::string_view
{
  auto start = value.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos) {
    return {};
  }
  auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(start, end - start + 1U);
}

auto ParseUnsigned(std::string_view value, uint32_t& out) -> bool
{
  uint32_t parsed = 0U;
  const auto* begin = value.data();
  const auto* end = value.data() + value.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc {} || result.ptr != end) {
    return false;
  }
  if (parsed == 0U) {
    return false;
  }
  out = parsed;
  return true;
}

auto SelectPipeline(ImportConcurrency& concurrency, const char key)
  -> ImportPipelineConcurrency*
{
  switch (key) {
  case 't':
    return &concurrency.texture;
  case 'b':
    return &concurrency.buffer;
  case 'm':
    return &concurrency.material;
  case 'h':
    return &concurrency.mesh_build;
  case 'g':
    return &concurrency.geometry;
  case 's':
    return &concurrency.scene;
  default:
    return nullptr;
  }
}

auto ParseConcurrencySpec(const std::string_view spec)
  -> std::expected<ImportConcurrency, std::error_code>
{
  ImportConcurrency result {};
  auto remaining = Trim(spec);
  if (remaining.empty()) {
    return result;
  }

  while (!remaining.empty()) {
    const auto comma = remaining.find(',');
    auto token = Trim(remaining.substr(0, comma));
    if (!token.empty()) {
      const auto colon = token.find(':');
      if (colon == std::string_view::npos || colon == 0U) {
        return std::unexpected(
          std::make_error_code(std::errc::invalid_argument));
      }
      const auto lowered
        = std::tolower(static_cast<unsigned char>(token.front()));
      const char key = static_cast<char>(lowered);
      auto* pipeline = SelectPipeline(result, key);
      if (pipeline == nullptr) {
        return std::unexpected(
          std::make_error_code(std::errc::invalid_argument));
      }

      auto spec_value = Trim(token.substr(colon + 1U));
      if (spec_value.empty()) {
        return std::unexpected(
          std::make_error_code(std::errc::invalid_argument));
      }

      const auto slash = spec_value.find('/');
      const auto workers_view = Trim(spec_value.substr(0, slash));
      uint32_t workers = 0U;
      if (!ParseUnsigned(workers_view, workers)) {
        return std::unexpected(
          std::make_error_code(std::errc::invalid_argument));
      }
      pipeline->workers = workers;

      if (slash != std::string_view::npos) {
        auto capacity_view = Trim(spec_value.substr(slash + 1U));
        if (capacity_view.empty()) {
          return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
        }
        uint32_t capacity = 0U;
        if (!ParseUnsigned(capacity_view, capacity)) {
          return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
        }
        pipeline->queue_capacity = capacity;
      }
    }

    if (comma == std::string_view::npos) {
      break;
    }
    remaining = remaining.substr(comma + 1U);
  }

  return result;
}

auto ApplyServiceConfigOverrides(const CommandLineContext& context,
  const GlobalOptions& global_options,
  AsyncImportService::Config& service_config, int& exit_code,
  bool& thread_pool_size_set, bool& concurrency_override_set) -> bool
{
  thread_pool_size_set = context.ovm.HasOption("thread-pool-size");
  concurrency_override_set = context.ovm.HasOption("concurrency");

  if (thread_pool_size_set) {
    const auto& values = context.ovm.ValuesOf("thread-pool-size");
    if (!values.empty()) {
      service_config.thread_pool_size = values.back().GetAs<uint32_t>();
    }
  }

  if (!concurrency_override_set) {
    return true;
  }

  const auto& values = context.ovm.ValuesOf("concurrency");
  if (values.empty()) {
    concurrency_override_set = false;
    return true;
  }

  const auto& spec = values.back().GetAs<std::string>();
  const auto parsed = ParseConcurrencySpec(spec);
  if (!parsed.has_value()) {
    constexpr std::string_view kMessage
      = "ERROR: invalid --concurrency specification";
    const auto reported = global_options.writer != nullptr
      ? global_options.writer->Error(kMessage)
      : false;
    if (!reported) {
      std::cerr << kMessage << "\n";
    }
    exit_code = 1;
    return false;
  }

  service_config.concurrency = *parsed;
  return true;
}

auto CreateImportService(ImportCommand& active_command,
  const AsyncImportService::Config& service_config, bool thread_pool_size_set,
  bool concurrency_override_set, int& exit_code, bool& options_valid)
  -> std::unique_ptr<AsyncImportService>
{
  std::unique_ptr<AsyncImportService> service_owner;
  const auto config = active_command.PrepareImportServiceConfig();
  if (!config.has_value()) {
    exit_code = ExitStatusFromError(config.error());
    options_valid = false;
    return nullptr;
  }
  auto final_config = *config;
  if (thread_pool_size_set) {
    final_config.thread_pool_size = service_config.thread_pool_size;
  }
  if (concurrency_override_set) {
    final_config.concurrency = service_config.concurrency;
  }
  service_owner = std::make_unique<AsyncImportService>(final_config);

  return service_owner;
}

auto StopReasonMessage(const int signal) -> std::string_view
{
  switch (signal) {
  case SIGINT:
    return "Stopping: interrupted (SIGINT)";
  case SIGTERM:
    return "Stopping: terminated (SIGTERM)";
  default:
    return "Stopping: interrupted";
  }
}

auto ResetStopState() -> void
{
  g_stop_requested.store(false, std::memory_order_relaxed);
  g_stop_handled.store(false, std::memory_order_relaxed);
  g_stop_signal.store(0, std::memory_order_relaxed);
  g_stop_count.store(0, std::memory_order_relaxed);
}

auto StartStopWatcher(AsyncImportService* service,
  oxygen::observer_ptr<IMessageWriter> writer) -> std::jthread
{
  return std::jthread([service, writer](std::stop_token st) {
    while (!st.stop_requested()) {
      if (g_stop_requested.load(std::memory_order_relaxed)) {
        if (g_stop_handled.exchange(true, std::memory_order_acq_rel)) {
          break;
        }
        if (service != nullptr) {
          if (writer != nullptr) {
            const auto message = StopReasonMessage(
              g_stop_signal.load(std::memory_order_relaxed));
            writer->Report(message);
          }
          service->CancelAll();
          service->RequestShutdown();
        }
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  });
}

auto FindCommand(std::string_view command_path,
  const std::vector<ImportCommand*>& commands) -> ImportCommand*
{
  for (auto* command : commands) {
    if (command_path == command->Name()) {
      return command;
    }
  }
  return nullptr;
}

auto RunSelectedCommand(ImportCommand& active_command, int& exit_code) -> void
{
  const auto result = active_command.Run();
  if (result.has_value()) {
    exit_code = 0;
  } else {
    exit_code = ExitStatusFromError(result.error());
  }
}

class ConsoleMessageWriter final : public IMessageWriter {
public:
  explicit ConsoleMessageWriter(bool quiet, bool no_color)
    : quiet_(quiet)
    , no_color_(no_color)
  {
    fmt::print(stdout, "\x1b[?25l");
    std::cout.flush();
  }

  ~ConsoleMessageWriter() override
  {
    fmt::print(stdout, "\x1b[?25h");
    std::cout.flush();
  }

  auto Error(std::string_view message) -> bool override
  {
    std::scoped_lock lock(mutex_);
    ClearProgressLine();
    if (no_color_) {
      std::cerr << "\r" << kErrorGlyph << ' ' << message << "\r\n";
    } else {
      fmt::print(stderr, fmt::fg(fmt::rgb(220, 38, 38)), "\r{} {}\r\n",
        kErrorGlyph, std::string(message));
    }
    last_was_progress_ = false;
    return true;
  }

  auto Warning(std::string_view message) -> bool override
  {
    std::scoped_lock lock(mutex_);
    ClearProgressLine();
    if (no_color_) {
      std::cerr << "\r" << kWarningGlyph << ' ' << message << "\r\n";
    } else {
      fmt::print(stderr, fmt::fg(fmt::color::yellow), "\r{} {}\r\n",
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
      std::cout << "\r" << message << "\r\n";
    } else {
      fmt::print(
        stdout, fmt::fg(fmt::color::white), "\r{}\r\n", std::string(message));
    }
    last_was_progress_ = false;
    return true;
  }

  auto Report(std::string_view message) -> bool override
  {
    std::scoped_lock lock(mutex_);
    ClearProgressLine();
    if (no_color_) {
      std::cout << "\r" << kSuccessGlyph << ' ' << message << "\r\n";
    } else {
      fmt::print(stdout, fmt::fg(fmt::color::cyan), "\r{} {}\r\n",
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
    const auto frame = NextSpinnerFrame();
    const auto safe_message = SanitizeProgressMessage(message);
    const auto text = fmt::format("{} {}", frame, safe_message);
    const auto width = GetTerminalWidth();
    std::string line = FitToWidth(text, width);
    if (no_color_) {
      fmt::print(stdout, "\r{}", line);
    } else {
      fmt::print(stdout, fmt::emphasis::faint | fmt::fg(fmt::color::white),
        "\r{}", line);
    }
    std::cout.flush();
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

  auto ClearProgressLine() -> void
  {
    if (last_was_progress_) {
      fmt::print(stdout, "\r");
      if (last_progress_len_ > 0U) {
        fmt::print(stdout, "{}", std::string(last_progress_len_, ' '));
        fmt::print(stdout, "\r");
      }
      std::cout.flush();
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
    return kFrames[index];
  }

  static constexpr std::string_view kErrorGlyph = "×";
  static constexpr std::string_view kWarningGlyph = "▲";
  static constexpr std::string_view kSuccessGlyph = "✓";

  std::mutex mutex_;
  const bool quiet_;
  const bool no_color_;
  bool last_was_progress_ = false;
  size_t last_progress_len_ = 0U;
  static inline std::atomic<size_t> spinner_index_ { 0U };
};

class MutedMessageWriter final : public IMessageWriter {
public:
  auto Error(std::string_view) -> bool override { return false; }
  auto Warning(std::string_view) -> bool override { return false; }
  auto Info(std::string_view) -> bool override { return false; }
  auto Report(std::string_view) -> bool override { return false; }
  auto Progress(std::string_view) -> bool override { return false; }
};

auto CreateMessageWriter(const GlobalOptions& global_options)
  -> std::unique_ptr<IMessageWriter>
{
  if (global_options.no_tui) {
    return std::make_unique<ConsoleMessageWriter>(
      global_options.quiet, global_options.no_color);
  }
  return std::make_unique<MutedMessageWriter>();
}
} // namespace

auto main(int argc, char** argv) -> int
{
  std::signal(SIGINT, HandleStopSignal);
  std::signal(SIGTERM, HandleStopSignal);

  loguru::g_preamble_date = false;
  loguru::g_preamble_file = true;
  loguru::g_preamble_verbose = false;
  loguru::g_preamble_time = true;
  loguru::g_preamble_uptime = false;
  loguru::g_preamble_thread = true;
  loguru::g_preamble_header = false;
  loguru::g_global_verbosity = loguru::Verbosity_OFF;

  loguru::init(argc, const_cast<const char**>(argv));
  loguru::set_thread_name("main");

  int exit_code = 0;
  try {
    using oxygen::content::import::tool::BuildCli;
    using oxygen::content::import::tool::FbxCommand;
    using oxygen::content::import::tool::GltfCommand;
    using oxygen::content::import::tool::TextureCommand;

    GlobalOptions global_options;
    BatchCommand batch_command(&global_options);
    FbxCommand fbx_command(&global_options);
    GltfCommand gltf_command(&global_options);
    TextureCommand texture_command(&global_options);
    std::vector<ImportCommand*> commands {
      &texture_command,
      &fbx_command,
      &gltf_command,
      &batch_command,
    };

    AsyncImportService::Config service_config {};
    bool thread_pool_size_set = false;
    bool concurrency_override_set = false;

    const auto cli = BuildCli(commands, global_options);
    const auto context = cli->Parse(argc, const_cast<const char**>(argv));

    FinalizeGlobalOptions(context);

    ApplyLoggingOptions(global_options);

    // Instantiate and keep ownership here in main.
    auto writer_owner = CreateMessageWriter(global_options);
    global_options.writer
      = oxygen::observer_ptr<IMessageWriter>(writer_owner.get());

    const auto command_path = context.active_command->PathAsString();
    auto* active_command = FindCommand(command_path, commands);

    bool options_valid
      = ApplyServiceConfigOverrides(context, global_options, service_config,
        exit_code, thread_pool_size_set, concurrency_override_set);

    batch_command.SetServiceConfigOverrides(
      &service_config, concurrency_override_set);

    std::unique_ptr<AsyncImportService> service_owner;
    std::optional<std::jthread> stop_watcher;
    if (IsMetaCommand(command_path, context)) {
      exit_code = 0;
    } else if (options_valid) {
      if (active_command == nullptr) {
        std::cerr << "ERROR: Unknown command\n";
        exit_code = 1;
        options_valid = false;
      }
    }

    if (options_valid) {
      service_owner = CreateImportService(*active_command, service_config,
        thread_pool_size_set, concurrency_override_set, exit_code,
        options_valid);

      if (service_owner != nullptr) {
        global_options.import_service
          = oxygen::observer_ptr<AsyncImportService>(service_owner.get());
        ResetStopState();
        stop_watcher.emplace(
          StartStopWatcher(service_owner.get(), global_options.writer));
      }

      if (options_valid) {
        RunSelectedCommand(*active_command, exit_code);
      }
    }
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: " << ex.what() << "\n";
    exit_code = 2;
  }

  loguru::flush();
  loguru::g_global_verbosity = loguru::Verbosity_OFF;
  loguru::shutdown();

  return exit_code;
}
