//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_GROUP_REDUCTION_HLSLI
#define OXYGEN_VORTEX_GROUP_REDUCTION_HLSLI

// All 64 lanes must call the generated function. The caller initializes shared
// storage and supplies a barrier-free Combine(left, right) operation. Fused
// tuples stay together: one synchronization per rung, with left operand first.
// No array arguments: HLSL parameter copies must not replace groupshared storage.
#define OXYGEN_DETAIL_GROUP_REDUCTION_64(Name, Combine, Stop, LoopAttribute) \
    static void Name(uint lane) \
    { \
        GroupMemoryBarrierWithGroupSync(); \
        LoopAttribute for (uint offset = 32u; offset > Stop; offset >>= 1u) { \
            if (lane < offset) { \
                Combine(lane, lane + offset); \
            } \
            GroupMemoryBarrierWithGroupSync(); \
        } \
    }

// Preserve the caller's existing compiler loop policy.
#define OXYGEN_DEFINE_GROUP_REDUCTION_64(Name, Combine) \
    OXYGEN_DETAIL_GROUP_REDUCTION_64(Name, Combine, 0u, )
#define OXYGEN_DEFINE_UNROLLED_GROUP_REDUCTION_64(Name, Combine) \
    OXYGEN_DETAIL_GROUP_REDUCTION_64(Name, Combine, 0u, [unroll])

// Leaves elements 0 and 1 for a lane-zero final pair. Every lane participates
// in the offset-2 barrier; there is no offset-1 shared write or extra barrier.
#define OXYGEN_DEFINE_GROUP_REDUCTION_64_TO_PAIR(Name, Combine) \
    OXYGEN_DETAIL_GROUP_REDUCTION_64(Name, Combine, 1u, [unroll])

#endif // OXYGEN_VORTEX_GROUP_REDUCTION_HLSLI
