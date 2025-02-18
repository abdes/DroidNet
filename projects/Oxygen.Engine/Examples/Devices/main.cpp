//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdlib>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DeviceManager.h>

// Force link the DLL containing the InitializeTypeRegistry function.
extern "C" auto InitializeTypeRegistry() -> oxygen::TypeRegistry*;
namespace {
[[maybe_unused]] auto* ts_registry_unused = InitializeTypeRegistry();
} // namespace

using oxygen::graphics::d3d12::DeviceManager;
using oxygen::graphics::d3d12::DeviceManagerDesc;

auto main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) -> int
{

    auto status { EXIT_SUCCESS };

#if defined(_MSC_VER)
    // Enable memory leak detection in debug mode
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    loguru::g_preamble_date = false;
    loguru::g_preamble_file = true;
    loguru::g_preamble_verbose = false;
    loguru::g_preamble_time = false;
    loguru::g_preamble_uptime = false;
    loguru::g_preamble_thread = false;
    loguru::g_preamble_header = false;
    loguru::g_stderr_verbosity = loguru::Verbosity_1;
    loguru::g_colorlogtostderr = true;
    // Optional, but useful to time-stamp the start of the log.
    // Will also detect verbosity level on command line as -v.
    loguru::init(argc, argv);

    {
        const oxygen::graphics::d3d12::DeviceManagerDesc props {
            .enable_debug = true,
            .enable_validation = false,
            .require_display = true,
            .auto_select_adapter = false,
            .minFeatureLevel = D3D_FEATURE_LEVEL_12_0,
        };
        DeviceManager device_manager(props);
        device_manager.SelectBestAdapter();
        device_manager.SelectAdapter(device_manager.Adapters().back().UniqueId());
        device_manager.SelectBestAdapter();
        device_manager.SelectBestAdapter();
    }

    LOG_F(INFO, "Exit with status: {}", status);
    loguru::shutdown();
    return status;
}
