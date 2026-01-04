//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#if defined(_MSC_VER)
#  include <crtdbg.h>
#endif

#include <windows.h>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/StringUtils.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Bake.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/DxcShaderCompiler.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Inspect.h>

using oxygen::graphics::d3d12::tools::shader_bake::DxcShaderCompiler;

namespace {

auto RunShaderBakeCli(std::span<const char*> args) -> int;

constexpr std::string_view kDefaultOxslOutputPath = "bin/Oxygen/shaders.bin";
constexpr std::string_view kDefaultShaderSourceRoot
  = "src/Oxygen/Graphics/Direct3D12/Shaders";
constexpr std::string_view kDefaultOxygenIncludeRoot = "src/Oxygen";

using oxygen::graphics::d3d12::tools::shader_bake::BakeArgs;
using oxygen::graphics::d3d12::tools::shader_bake::InspectArgs;

struct BakeCliStorage {
  std::string workspace_root_string;
  std::string out_file_string;
  std::string shader_root_string;
  std::string oxygen_include_root_string;
};

struct InspectCliStorage {
  std::string file_string;

  bool header_only { false };
  bool modules_only { false };
  bool show_defines { false };
  bool show_offsets { false };
  bool show_reflection { false };
  bool show_all { false };
};

auto AddBakeOptions(
  oxygen::clap::CommandBuilder& command, BakeCliStorage& storage) -> void
{
  using namespace oxygen::clap;

  command
    .WithOption(OptionBuilder("workspace_root")
        .Long("workspace-root")
        .About("Workspace root (repo root).")
        .WithValue<std::string>()
        .StoreTo(&storage.workspace_root_string)
        .Build())
    .WithOption(OptionBuilder("shader_root")
        .Long("shader-root")
        .About("Shader source root (relative to workspace root "
               "if relative).")
        .WithValue<std::string>()
        .StoreTo(&storage.shader_root_string)
        .Build())
    .WithOption(OptionBuilder("oxygen_include_root")
        .Long("oxygen-include-root")
        .About("Oxygen include root (relative to workspace "
               "root if relative).")
        .WithValue<std::string>()
        .StoreTo(&storage.oxygen_include_root_string)
        .Build())
    .WithOption(OptionBuilder("include_dir")
        .Long("include-dir")
        .About("Additional include directory (relative to "
               "workspace root if relative). Can be repeated.")
        .WithValue<std::string>()
        .Repeatable()
        .Build())
    .WithOption(OptionBuilder("out")
        .Long("out")
        .About("Output shaders.bin path (relative to workspace "
               "root if relative).")
        .WithValue<std::string>()
        .StoreTo(&storage.out_file_string)
        .Build());
}

auto AddInspectOptions(
  oxygen::clap::CommandBuilder& command, InspectCliStorage& storage) -> void
{
  using namespace oxygen::clap;

  command
    .WithOption(OptionBuilder("file")
        .Short("f")
        .Long("file")
        .About("Path to a shader library file (OXSL v1).")
        .WithValue<std::string>()
        .StoreTo(&storage.file_string)
        .Build())
    .WithOption(OptionBuilder("header")
        .Short("H")
        .Long("header")
        .About("Include the library header.")
        .WithValue<bool>()
        .StoreTo(&storage.header_only)
        .DefaultValue(false)
        .Build())
    .WithOption(OptionBuilder("modules")
        .Short("m")
        .Long("modules")
        .About("Include the module list.")
        .WithValue<bool>()
        .StoreTo(&storage.modules_only)
        .DefaultValue(false)
        .Build())
    .WithOption(OptionBuilder("defines")
        .Short("d")
        .Long("defines")
        .About("Include per-module defines.")
        .WithValue<bool>()
        .StoreTo(&storage.show_defines)
        .DefaultValue(false)
        .Build())
    .WithOption(OptionBuilder("offsets")
        .Short("o")
        .Long("offsets")
        .About("Include payload offsets/sizes.")
        .WithValue<bool>()
        .StoreTo(&storage.show_offsets)
        .DefaultValue(false)
        .Build());

  command.WithOption(OptionBuilder("reflection")
      .Short("r")
      .Long("reflection")
      .About("Include decoded reflection info (OXRF).")
      .WithValue<bool>()
      .StoreTo(&storage.show_reflection)
      .DefaultValue(false)
      .Build());

  command.WithOption(OptionBuilder("all")
      .Short("a")
      .Long("all")
      .About("Include defines, offsets, and reflection.")
      .WithValue<bool>()
      .StoreTo(&storage.show_all)
      .DefaultValue(false)
      .Build());
}

auto ParseBakeArgs(const BakeCliStorage& storage,
  const oxygen::clap::CommandLineContext& context) -> BakeArgs
{
  std::filesystem::path workspace_root(storage.workspace_root_string);
  std::filesystem::path out_file(storage.out_file_string);
  std::filesystem::path shader_root(storage.shader_root_string);
  std::filesystem::path oxygen_include_root(storage.oxygen_include_root_string);

  if (workspace_root.empty()) {
    throw std::runtime_error("--workspace-root is required");
  }

  if (shader_root.empty()) {
    shader_root = std::filesystem::path(kDefaultShaderSourceRoot);
  }
  if (shader_root.is_relative()) {
    shader_root = workspace_root / shader_root;
  }

  if (oxygen_include_root.empty()) {
    oxygen_include_root = std::filesystem::path(kDefaultOxygenIncludeRoot);
  }
  if (oxygen_include_root.is_relative()) {
    oxygen_include_root = workspace_root / oxygen_include_root;
  }

  std::vector<std::filesystem::path> extra_include_dirs;
  for (const auto& value : context.ovm.ValuesOf("include_dir")) {
    std::filesystem::path include_dir(value.GetAs<std::string>());
    if (include_dir.is_relative()) {
      include_dir = workspace_root / include_dir;
    }
    extra_include_dirs.push_back(std::move(include_dir));
  }

  if (out_file.empty()) {
    out_file = std::filesystem::path(kDefaultOxslOutputPath);
  }
  if (out_file.is_relative()) {
    out_file = workspace_root / out_file;
  }

  return BakeArgs {
    .workspace_root = std::move(workspace_root),
    .out_file = std::move(out_file),
    .shader_source_root = std::move(shader_root),
    .oxygen_include_root = std::move(oxygen_include_root),
    .extra_include_dirs = std::move(extra_include_dirs),
  };
}

auto RunBakeCommand(const oxygen::clap::CommandLineContext& context,
  const BakeCliStorage& storage) -> int
{
  const auto bake_args = ParseBakeArgs(storage, context);
  return oxygen::graphics::d3d12::tools::shader_bake::BakeShaderLibrary(
    bake_args);
}

auto RunInspectCommand(const InspectCliStorage& storage) -> int
{
  LOG_SCOPE_F(INFO, "ShaderInspect");

  if (storage.file_string.empty()) {
    throw std::runtime_error("--file is required");
  }

  InspectArgs args;
  args.file = std::filesystem::path(storage.file_string);
  args.header_only = storage.header_only;
  args.modules_only = storage.modules_only;
  args.show_defines = storage.show_defines;
  args.show_offsets = storage.show_offsets;
  args.show_reflection = storage.show_reflection;

  if (storage.show_all) {
    args.show_defines = true;
    args.show_offsets = true;
    args.show_reflection = true;
  }

  return oxygen::graphics::d3d12::tools::shader_bake::InspectShaderLibrary(
    args);
}

auto IsInspectCommand(const oxygen::clap::Command& command) -> bool
{
  if (command.IsDefault()) {
    return false;
  }

  const auto& path = command.Path();
  return !path.empty() && path.front() == "inspect";
}

auto RunShaderBakeCli(std::span<const char*> args) -> int
{
  using namespace oxygen::clap;

  const int argc = static_cast<int>(args.size());
  const char** argv = args.data();

  BakeCliStorage bake_storage;
  InspectCliStorage inspect_storage;

  oxygen::clap::CommandBuilder default_bake { oxygen::clap::Command::DEFAULT };
  default_bake.About("Bake all engine shaders into shaders.bin");
  AddBakeOptions(default_bake, bake_storage);

  oxygen::clap::CommandBuilder bake { "bake" };
  bake.About("Bake all engine shaders into shaders.bin");
  AddBakeOptions(bake, bake_storage);

  oxygen::clap::CommandBuilder inspect { "inspect" };
  inspect.About("Inspect and print the contents of a shader library");
  AddInspectOptions(inspect, inspect_storage);

  auto cli = CliBuilder()
               .ProgramName("ShaderBake")
               .Version("0.1")
               .About("Build-time shader library producer (OXSL v1).")
               .WithHelpCommand()
               .WithVersionCommand()
               .WithCommand(default_bake.Build())
               .WithCommand(bake.Build())
               .WithCommand(inspect.Build())
               .Build();

  const auto context = cli->Parse(argc, argv);
  if (!context.active_command) {
    throw std::runtime_error("no active command selected");
  }

  // Clap prints help/version during parsing, but still returns a context.
  // Avoid running any command implementation in that case.
  if (context.ovm.HasOption(oxygen::clap::Command::HELP)
    || context.active_command->PathAsString() == oxygen::clap::Command::HELP
    || context.active_command->PathAsString()
      == oxygen::clap::Command::VERSION) {
    return 0;
  }

  if (IsInspectCommand(*context.active_command)) {
    return RunInspectCommand(inspect_storage);
  }

  return RunBakeCommand(context, bake_storage);
}

} // namespace

auto main(int argc, char** argv) noexcept -> int
{
#if defined(_MSC_VER)
  // Enable memory leak detection in debug mode.
  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

  // Pre-allocate static error messages when we are handling critical failures.
  constexpr std::string_view kUnhandledException
    = "Error: Out of memory or other critical failure when logging unhandled "
      "exception\n";
  constexpr std::string_view kUnknownUnhandledException
    = "Error: Out of memory or other critical failure when logging unhandled "
      "exception of unknown type\n";

  // Low-level error reporting function that won't allocate memory.
  auto report_error = [](std::string_view message) noexcept {
#if defined(_WIN32)
    HANDLE stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
    DWORD bytes_written { 0UL };
    WriteFile(stderr_handle, message.data(), static_cast<DWORD>(message.size()),
      &bytes_written, nullptr);
#else
    write(STDERR_FILENO, message.data(), message.size());
#endif
  };

  int exit_code = EXIT_FAILURE;

  try {
    loguru::g_preamble_date = false;
    loguru::g_preamble_file = true;
    loguru::g_preamble_verbose = false;
    loguru::g_preamble_time = false;
    loguru::g_preamble_uptime = false;
    loguru::g_preamble_thread = true;
    loguru::g_preamble_header = false;
    loguru::g_stderr_verbosity = loguru::Verbosity_0;
    loguru::g_colorlogtostderr = true;
    // Optional, but useful to time-stamp the start of the log.
    // Will also detect verbosity level on command line as -v.
    loguru::init(argc, const_cast<const char**>(argv));
    loguru::set_thread_name("shaderbake");

    exit_code = RunShaderBakeCli(
      std::span(const_cast<const char**>(argv), static_cast<size_t>(argc)));
  } catch (const std::exception& ex) {
    try {
      LOG_F(ERROR, "Unhandled exception: {}", ex.what());
    } catch (...) {
      report_error(kUnhandledException);
    }
  } catch (...) {
    try {
      LOG_F(ERROR, "Unhandled exception of unknown type");
    } catch (...) {
      report_error(kUnknownUnhandledException);
    }
  }

  return exit_code;
}
