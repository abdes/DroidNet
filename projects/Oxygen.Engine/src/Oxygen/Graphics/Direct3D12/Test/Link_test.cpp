//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/D3D12HeapAllocationStrategy.h>

using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::d3d12::D3D12HeapAllocationStrategy;

void SetupLoguru(int& argc, char** argv)
{
    loguru::g_preamble_date = false;
    loguru::g_preamble_file = true;
    loguru::g_preamble_verbose = false;
    loguru::g_preamble_time = false;
    loguru::g_preamble_uptime = false;
    loguru::g_preamble_thread = false;
    loguru::g_preamble_header = false;
    loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
    loguru::init(argc, argv);
    loguru::g_stderr_verbosity = loguru::Verbosity_1;
}

int main(int argc, char** argv)
{
    SetupLoguru(argc, argv);

    // Simplest usage: no real device, just use nullptr
    D3D12HeapAllocationStrategy strategy(nullptr);

    LOG_SCOPE_F(INFO, "Heap Example");
    {
        // Example: get a heap key for a CBV_SRV_UAV shader-visible descriptor
        auto key = strategy.GetHeapKey(
            ResourceViewType::kTexture_SRV,
            DescriptorVisibility::kShaderVisible);

        // Print the heap key
        LOG_F(INFO, "Heap key: {}", key);

        // Get the heap description for this key
        const auto& desc = strategy.GetHeapDescription(key);
        LOG_F(INFO, "Shader-visible capacity: {}", desc.shader_visible_capacity);

        // Get the base index for this heap
        auto base_index = strategy.GetHeapBaseIndex(
            ResourceViewType::kTexture_SRV,
            DescriptorVisibility::kShaderVisible);
        LOG_F(INFO, "Base index: {}", base_index);
    }

    return 0;
}
