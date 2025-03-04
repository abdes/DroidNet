//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <span>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DeviceManager.h>

using oxygen::graphics::d3d12::DeviceManager;
using oxygen::graphics::d3d12::DeviceManagerDesc;

extern "C" void MainImpl(std::span<const char*> /*args*/)
{
    const oxygen::graphics::d3d12::DeviceManagerDesc props {
        .enable_debug = true,
        .enable_validation = false,
        .require_display = true,
        .auto_select_adapter = false,
        .minFeatureLevel = D3D_FEATURE_LEVEL_12_0,
    };
    DeviceManager device_manager(props);

    // Select adapter and add device removal handler
    device_manager.SelectBestAdapter([]() {
        LOG_F(INFO, "Device removal detected!");
    });

    // Get the device
    auto* device = device_manager.Device();

    // Trigger a device removal
    device->GetDeviceRemovedReason(); // First call to check current state

    // Force device removal through debug layer
    device->RemoveDevice(); // This will trigger device removal

    // Try to use the device again - this should trigger the removal handler
    device_manager.SelectBestAdapter([]() {
        LOG_F(INFO, "Device removal handler called");
    });
}
