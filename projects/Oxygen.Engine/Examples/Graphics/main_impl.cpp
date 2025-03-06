//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <span>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DeviceManager.h>
#include <Oxygen/Loader/GraphicsBackendLoader.h>

using oxygen::GraphicsConfig;
using oxygen::graphics::BackendType;
using oxygen::graphics::d3d12::DeviceManager;
using oxygen::graphics::d3d12::DeviceManagerDesc;

namespace {
void LoadBackend(const GraphicsConfig& config)
{
    // Get the singleton instance of the GraphicsBackendLoader
    auto& loader = oxygen::GraphicsBackendLoader::GetInstance();
    // Load the backend using the singleton
    auto backend = loader.LoadBackend(BackendType::kDirect3D12, config);
    if (!backend.expired()) {
        LOG_F(INFO, "Successfully loaded the graphics backend");
    } else {
        LOG_F(ERROR, "Failed to load the graphics backend");
    }
}
}

extern "C" void MainImpl(std::span<const char*> /*args*/)
{
    GraphicsConfig config {
        .enable_debug = true,
        .enable_validation = false,
        .headless = true,
        .extra = {},
    };

    // Get the singleton instance of the GraphicsBackendLoader
    auto& loader = oxygen::GraphicsBackendLoader::GetInstance();
    // Load the backend using the singleton
    auto backend = loader.LoadBackend(BackendType::kDirect3D12, config);
    DCHECK_F(!backend.expired());

    // DeviceManager device_manager(props);

    //// Select adapter and add device removal handler
    // device_manager.SelectBestAdapter([]() {
    //     LOG_F(INFO, "Device removal detected!");
    // });

    //// Get the device
    // auto* device = device_manager.Device();

    //// Trigger a device removal
    // device->GetDeviceRemovedReason(); // First call to check current state

    //// Force device removal through debug layer
    // device->RemoveDevice(); // This will trigger device removal

    //// Try to use the device again - this should trigger the removal handler
    // device_manager.SelectBestAdapter([]() {
    //     LOG_F(INFO, "Device removal handler called");
    // });
}
