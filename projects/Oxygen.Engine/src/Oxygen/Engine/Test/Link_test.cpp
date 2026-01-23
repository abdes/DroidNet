//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iostream>
#include <memory>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Config/EngineConfig.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Config/PlatformConfig.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Loader/GraphicsBackendLoader.h>
#include <Oxygen/Platform/Platform.h>

using oxygen::AsyncEngine;
using oxygen::EngineConfig;
using oxygen::GraphicsConfig;
using oxygen::Platform;
using oxygen::PlatformConfig;
using oxygen::graphics::BackendType;

auto main(int argc, char** argv) -> int
{
  // Initialize loguru logging system
  loguru::g_preamble_date = false;
  loguru::g_preamble_file = true;
  loguru::g_preamble_verbose = false;
  loguru::g_preamble_time = false;
  loguru::g_preamble_uptime = false;
  loguru::g_preamble_thread = true;
  loguru::g_preamble_header = false;
  loguru::g_global_verbosity = loguru::Verbosity_0;
  loguru::g_colorlogtostderr = true;
  loguru::init(argc, const_cast<const char**>(argv));
  loguru::set_thread_name("main");

  const PlatformConfig platform_config {
    .headless = true,
    .thread_pool_size = 1,
  };
  auto platform = std::make_shared<Platform>(platform_config);

  const GraphicsConfig gfx_config {
    .enable_debug = false,
    .enable_validation = false,
    .headless = true,
    .extra = {},
  };
  auto& loader = oxygen::GraphicsBackendLoader::GetInstance();
  auto gfx_weak = loader.LoadBackend(BackendType::kHeadless, gfx_config);

  {
    const EngineConfig props {};
    AsyncEngine engine(platform, gfx_weak, props);
    std::cout << "AsyncEngine link test successful\n";
  }

  loader.UnloadBackend();
  platform.reset();

  // Ensure all logs are flushed before shutdown
  loguru::flush();

  return 0;
}
