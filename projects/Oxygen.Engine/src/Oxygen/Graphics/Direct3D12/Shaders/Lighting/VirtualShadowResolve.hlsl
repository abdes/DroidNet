//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialShadingConstants.hlsli"

#define BX_VERTEX_TYPE uint4
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct VirtualShadowResolvePassConstants
{
    uint request_words_srv_index;
    uint page_table_srv_index;
    uint schedule_uav_index;
    uint schedule_count_uav_index;
    uint request_word_count;
    uint total_page_count;
    uint schedule_capacity;
    uint _pad0;
};

static const uint kPageTableValidBit = (1u << 28u);

[shader("compute")]
[numthreads(64, 1, 1)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<VirtualShadowResolvePassConstants> pass_constants =
        ResourceDescriptorHeap[g_PassConstantsIndex];
    if (!BX_IN_GLOBAL_SRV(pass_constants.request_words_srv_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.page_table_srv_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.schedule_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.schedule_count_uav_index)) {
        return;
    }

    const uint word_index = dispatch_thread_id.x;
    if (word_index >= pass_constants.request_word_count) {
        return;
    }

    StructuredBuffer<uint> request_words =
        ResourceDescriptorHeap[pass_constants.request_words_srv_index];
    StructuredBuffer<uint> page_table =
        ResourceDescriptorHeap[pass_constants.page_table_srv_index];
    RWStructuredBuffer<uint4> schedule =
        ResourceDescriptorHeap[pass_constants.schedule_uav_index];
    RWStructuredBuffer<uint> schedule_count =
        ResourceDescriptorHeap[pass_constants.schedule_count_uav_index];

    uint request_word = request_words[word_index];
    while (request_word != 0u) {
        const uint bit_index = firstbitlow(request_word);
        const uint page_index = word_index * 32u + bit_index;
        if (page_index < pass_constants.total_page_count) {
            const uint packed_entry = page_table[page_index];
            if ((packed_entry & kPageTableValidBit) != 0u) {
                uint output_index = 0u;
                InterlockedAdd(schedule_count[0], 1u, output_index);
                if (output_index < pass_constants.schedule_capacity) {
                    const uint tile_x = packed_entry & 0x0FFFu;
                    const uint tile_y = (packed_entry >> 12u) & 0x0FFFu;
                    schedule[output_index] =
                        uint4(page_index, packed_entry, tile_x, tile_y);
                }
            }
        }

        request_word &= (request_word - 1u);
    }
}
