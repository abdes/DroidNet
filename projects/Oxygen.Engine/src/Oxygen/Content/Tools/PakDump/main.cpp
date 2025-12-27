//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/content/EngineTag.h>

#include "DumpContext.h"
#include "PakFileDumper.h"

namespace oxygen::content::internal {

auto EngineTagFactory::Get() noexcept -> EngineTag { return EngineTag {}; }

} // namespace oxygen::content::internal

static auto ParseCommandLine(int argc, const char* argv[]) -> DumpContext
{
  DumpContext ctx;

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--no-header") {
      ctx.show_header = false;
    } else if (arg == "--no-footer") {
      ctx.show_footer = false;
    } else if (arg == "--no-directory") {
      ctx.show_directory = false;
    } else if (arg == "--no-resources") {
      ctx.show_resources = false;
    } else if (arg == "--show-data") {
      ctx.show_resource_data = true;
    } else if (arg == "--hex-dump-assets") {
      ctx.show_asset_descriptors = true;
    } else if (arg == "--verbose") {
      ctx.verbose = true;
    } else if (arg.starts_with("--max-data=")) {
      ctx.max_data_bytes = std::stoul(arg.substr(11));
    }
  }

  return ctx;
}

static auto PrintUsage(const char* program_name) -> void
{
  std::cout << "Usage: " << program_name << " <pakfile> [options]\n";
  std::cout << "\nOptions:\n";
  std::cout << "  --no-header        Don't show PAK header information\n";
  std::cout << "  --no-footer        Don't show PAK footer information\n";
  std::cout << "  --no-directory     Don't show asset directory\n";
  std::cout << "  --no-resources     Don't show resource table information\n";
  std::cout << "  --show-data        Show hex dump of resource data "
               "(buffers/textures)\n";
  std::cout << "  --hex-dump-assets  Show hex dump of asset descriptors\n";
  std::cout << "  --verbose          Show detailed information\n";
  std::cout << "  --max-data=N       Maximum bytes to show for data dumps "
               "(default: 256)\n";
  std::cout << "\nExamples:\n";
  std::cout << "  " << program_name << " game.pak\n";
  std::cout << "  " << program_name << " game.pak --verbose --show-data\n";
  std::cout << "  " << program_name << " game.pak --hex-dump-assets\n";
  std::cout << "  " << program_name
            << " game.pak --verbose --show-data --hex-dump-assets\n";
}

auto main(int argc, char** argv) -> int
{
  using namespace oxygen::content;

  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
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

  // Parse command line options
  DumpContext ctx = ParseCommandLine(argc, const_cast<const char**>(argv));
  ctx.pak_path = std::filesystem::path(argv[1]);

  if (!std::filesystem::exists(ctx.pak_path)) {
    std::cerr << "File not found: " << ctx.pak_path << "\n";
    return 1;
  }

  using oxygen::content::internal::EngineTagFactory;

  try {
    PakFile pak(ctx.pak_path);
    AssetLoaderConfig loader_config { .work_offline = true };
    AssetLoader asset_loader(EngineTagFactory::Get(), loader_config);
    asset_loader.AddPakFile(ctx.pak_path);
    PakFileDumper dumper(ctx);
    dumper.Dump(pak, asset_loader);
  } catch (const std::exception& ex) {
    std::cerr << "Error: " << ex.what() << "\n";
    return 2;
  }
  return 0;
}
