//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Fluent/CliBuilder.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/EngineTag.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/OxCo/asio.h>

#include "DumpContext.h"
#include "PakFileDumper.h"

namespace oxygen::content::internal {

auto EngineTagFactory::Get() noexcept -> EngineTag { return EngineTag {}; }

} // namespace oxygen::content::internal

namespace {

using oxygen::clap::Cli;
using oxygen::clap::CliBuilder;
using oxygen::clap::Command;
using oxygen::clap::CommandBuilder;
using oxygen::clap::Option;

constexpr std::string_view kProgramName = "Oxygen.Content.PakDump";
constexpr std::string_view kVersion = "0.1";

struct PakDumpOptions {
  std::string pakfile;
  bool no_header = false;
  bool no_footer = false;
  bool no_directory = false;
  bool no_resources = false;
  bool show_data = false;
  bool hex_dump_assets = false;
  bool verbose = false;
  size_t max_data_bytes = 256;
};

auto BuildCli(PakDumpOptions& opts) -> std::unique_ptr<Cli>
{
  const auto pakfile_positional = Option::Positional("pakfile")
                                    .About("Path to .pak file")
                                    .Required()
                                    .WithValue<std::string>()
                                    .StoreTo(&opts.pakfile)
                                    .Build();

  const std::shared_ptr<Command> default_command
    = CommandBuilder(Command::DEFAULT)
        .About("Dump and inspect a PAK content archive.")
        .WithPositionalArguments(pakfile_positional)
        .WithOption(Option::WithKey("no-header")
            .About("Don't show PAK header information")
            .Long("no-header")
            .WithValue<bool>()
            .StoreTo(&opts.no_header)
            .Build())
        .WithOption(Option::WithKey("no-footer")
            .About("Don't show PAK footer information")
            .Long("no-footer")
            .WithValue<bool>()
            .StoreTo(&opts.no_footer)
            .Build())
        .WithOption(Option::WithKey("no-directory")
            .About("Don't show asset directory")
            .Long("no-directory")
            .WithValue<bool>()
            .StoreTo(&opts.no_directory)
            .Build())
        .WithOption(Option::WithKey("no-resources")
            .About("Don't show resource table information")
            .Long("no-resources")
            .WithValue<bool>()
            .StoreTo(&opts.no_resources)
            .Build())
        .WithOption(Option::WithKey("show-data")
            .About("Show hex dump of resource data (buffers/textures)")
            .Long("show-data")
            .WithValue<bool>()
            .StoreTo(&opts.show_data)
            .Build())
        .WithOption(Option::WithKey("hex-dump-assets")
            .About("Show hex dump of asset descriptors")
            .Long("hex-dump-assets")
            .WithValue<bool>()
            .StoreTo(&opts.hex_dump_assets)
            .Build())
        .WithOption(Option::WithKey("verbose")
            .About("Show detailed information")
            .Long("verbose")
            .WithValue<bool>()
            .StoreTo(&opts.verbose)
            .Build())
        .WithOption(Option::WithKey("max-data")
            .About("Maximum bytes to show for data dumps")
            .Long("max-data")
            .WithValue<size_t>()
            .DefaultValue(256)
            .StoreTo(&opts.max_data_bytes)
            .Build());

  return CliBuilder()
    .ProgramName(std::string(kProgramName))
    .Version(std::string(kVersion))
    .About("Developer diagnostics utility for Oxygen .pak content archives.")
    .WithHelpCommand()
    .WithVersionCommand()
    .WithCommand(default_command)
    .Build();
}

} // namespace

auto main(int argc, char** argv) -> int
{
  using namespace oxygen::content;
  using oxygen::observer_ptr;

  PakDumpOptions opts;
  const auto cli = BuildCli(opts);

  try {
    const auto context = cli->Parse(argc, const_cast<const char**>(argv));

    const auto command_path = context.active_command->PathAsString();
    const auto& ovm = context.ovm;

    if (command_path == Command::VERSION || command_path == Command::HELP
      || ovm.HasOption(Command::HELP)) {
      return 0;
    }

    // Configure logging
    loguru::g_preamble_date = false;
    loguru::g_preamble_file = true;
    loguru::g_preamble_verbose = false;
    loguru::g_preamble_time = false;
    loguru::g_preamble_uptime = false;
    loguru::g_preamble_thread = false;
    loguru::g_preamble_header = false;
    loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
    loguru::g_colorlogtostderr = true;
    loguru::init(argc, const_cast<const char**>(argv));
    loguru::set_thread_name("main");

    // Map parsed options to dump context.
    DumpContext ctx;
    ctx.show_header = !opts.no_header;
    ctx.show_footer = !opts.no_footer;
    ctx.show_directory = !opts.no_directory;
    ctx.show_resources = !opts.no_resources;
    ctx.show_resource_data = opts.show_data;
    ctx.show_asset_descriptors = opts.hex_dump_assets;
    ctx.verbose = opts.verbose;
    ctx.max_data_bytes = opts.max_data_bytes;
    ctx.pak_path = std::filesystem::path(opts.pakfile);

    if (!std::filesystem::exists(ctx.pak_path)) {
      std::cerr << "File not found: " << ctx.pak_path << "\n";
      return 1;
    }

    using oxygen::content::internal::EngineTagFactory;

    PakFile pak(ctx.pak_path);

    asio::io_context io;
    (oxygen::co::Run)(io, [&]() -> oxygen::co::Co<> {
      oxygen::co::ThreadPool pool(io, 2);
      AssetLoaderConfig loader_config {
        .thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool },
        .work_offline = true,
      };

      AssetLoader asset_loader(EngineTagFactory::Get(), loader_config);
      asset_loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);
      asset_loader.RegisterLoader(
        oxygen::content::loaders::LoadTextureResource);

      OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &asset_loader);
        asset_loader.Run();

        asset_loader.AddPakFile(ctx.pak_path);
        PakFileDumper dumper(ctx);
        co_await dumper.DumpAsync(pak, asset_loader);

        asset_loader.Stop();
        co_return oxygen::co::kJoin;
      };
    });

    return 0;
  } catch (const std::exception& /*ex*/) {
    // The error is already printed by the CLI parser.
    return 2;
  }
}
