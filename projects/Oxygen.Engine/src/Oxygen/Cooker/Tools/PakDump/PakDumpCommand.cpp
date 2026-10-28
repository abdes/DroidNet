//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>

#include "DumpContext.h"
#include "PakDumpCommand.h"
#include "PakFileDumper.h"
#include <asio/io_context.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/PhysicsResourceLoader.h>
#include <Oxygen/Content/Loaders/ScriptLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/OxCo/asio.h> // IWYU pragma: keep

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
} // namespace oxygen::engine::internal

namespace oxygen::content::pakdump {

using clap::CommandBuilder;
using clap::Option;

auto PakDumpCommand::Name() const -> std::string_view
{
  return clap::Command::DEFAULT;
}

auto PakDumpCommand::BuildCommand() -> std::shared_ptr<clap::Command>
{
  return CommandBuilder(clap::Command::DEFAULT)
    .About("Dump and inspect a PAK content archive.")
    .WithPositionalArguments(Option::Positional("pakfile")
        .About("Path to .pak file")
        .Required()
        .WithValue<std::string>()
        .StoreTo(&pakfile_)
        .Build())
    .WithOption(Option::WithKey("mount")
        .About("Mount a pak the dumped pak depends on, below it; repeat "
               "in the order the dependencies are declared")
        .Long("mount")
        .WithValue<std::filesystem::path>()
        .UserFriendlyName("PAK")
        .Repeatable()
        .CallOnEachValue([this](const std::filesystem::path& path) -> void {
          mounts_.push_back(path);
        })
        .Build())
    .WithOption(Option::WithKey("no-header")
        .About("Don't show PAK header information")
        .Long("no-header")
        .WithValue<bool>()
        .StoreTo(&no_header_)
        .Build())
    .WithOption(Option::WithKey("no-footer")
        .About("Don't show PAK footer information")
        .Long("no-footer")
        .WithValue<bool>()
        .StoreTo(&no_footer_)
        .Build())
    .WithOption(Option::WithKey("no-directory")
        .About("Don't show asset directory")
        .Long("no-directory")
        .WithValue<bool>()
        .StoreTo(&no_directory_)
        .Build())
    .WithOption(Option::WithKey("no-resources")
        .About("Don't show resource table information")
        .Long("no-resources")
        .WithValue<bool>()
        .StoreTo(&no_resources_)
        .Build())
    .WithOption(Option::WithKey("show-data")
        .About("Show hex dump of resource data (buffers/textures/scripts)")
        .Long("show-data")
        .WithValue<bool>()
        .StoreTo(&show_data_)
        .Build())
    .WithOption(Option::WithKey("hex-dump-assets")
        .About("Show hex dump of asset descriptors")
        .Long("hex-dump-assets")
        .WithValue<bool>()
        .StoreTo(&hex_dump_assets_)
        .Build())
    .WithOption(Option::WithKey("verbose")
        .About("Show detailed information")
        .Long("verbose")
        .WithValue<bool>()
        .StoreTo(&verbose_)
        .Build())
    .WithOption(Option::WithKey("max-data")
        .About("Maximum bytes to show for data dumps")
        .Long("max-data")
        .WithValue<size_t>()
        .DefaultValue(256)
        .StoreTo(&max_data_bytes_)
        .Build());
}

auto PakDumpCommand::Run(toolkit::ToolContext& context) -> toolkit::ToolResult
{
  using oxygen::observer_ptr;

  DumpContext ctx;
  ctx.show_header = !no_header_;
  ctx.show_footer = !no_footer_;
  ctx.show_directory = !no_directory_;
  ctx.show_resources = !no_resources_;
  ctx.show_resource_data = show_data_;
  ctx.show_asset_descriptors = hex_dump_assets_;
  ctx.verbose = verbose_;
  ctx.max_data_bytes = max_data_bytes_;
  ctx.pak_path = std::filesystem::path(pakfile_);
  ctx.streams = toolkit::ToolStreams {
    .out = observer_ptr<std::ostream> { &context.Out() },
    .err = observer_ptr<std::ostream> { &context.Err() },
  };

  for (const auto& path : mounts_) {
    if (!std::filesystem::exists(path)) {
      return std::unexpected(toolkit::ToolError::Usage(
        std::format("File not found: {}", path.string())));
    }
  }
  if (!std::filesystem::exists(ctx.pak_path)) {
    return std::unexpected(toolkit::ToolError::Usage(
      std::format("File not found: {}", ctx.pak_path.string())));
  }

  using oxygen::engine::internal::EngineTagFactory;

  PakFile pak(ctx.pak_path);

  bool succeeded = false;
  asio::io_context io;
  oxygen::co::Run(io, [&] -> oxygen::co::Co<> {
    oxygen::co::ThreadPool pool(io, 2);
    AssetLoaderConfig loader_config {
      .thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool },
      .work_offline = true,
    };

    AssetLoader asset_loader(EngineTagFactory::Get(), loader_config);
    asset_loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);
    asset_loader.RegisterLoader(oxygen::content::loaders::LoadScriptResource);
    asset_loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
    asset_loader.RegisterLoader(oxygen::content::loaders::LoadPhysicsResource);

    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &asset_loader);
      asset_loader.Run();

      for (const auto& path : mounts_) {
        asset_loader.AddPakFile(path);
      }
      asset_loader.AddPakFile(ctx.pak_path);
      PakFileDumper dumper(ctx);
      succeeded = co_await dumper.DumpAsync(pak, asset_loader);

      asset_loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });

  if (!succeeded) {
    return std::unexpected(toolkit::ToolError::Failed());
  }
  return {};
}

} // namespace oxygen::content::pakdump
