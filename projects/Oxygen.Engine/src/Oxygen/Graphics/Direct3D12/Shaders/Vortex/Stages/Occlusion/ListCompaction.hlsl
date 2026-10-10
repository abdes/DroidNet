//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Order-preserving stream compaction of indirect draw candidates.
//
// A list is a CPU-ordered array of candidate draws, cut into segments that
// share one pipeline. Three dispatches keep the candidates whose visibility
// bits match the predicate, in candidate order:
//
// 1. Scan: one 256-candidate group scans its keep flags and writes each
//    candidate's group-local exclusive prefix and the group total.
// 2. Groups: one group turns the group totals into exclusive group prefixes.
// 3. Scatter: each kept candidate writes its command at its segment's first
//    command plus its rank within the segment; the segment's last candidate
//    writes the segment count to the separate count buffer.
//
// No step depends on scheduling order, so the lists are deterministic.

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/Draw/DrawVisibility.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

static const uint LIST_COMPACTION_GROUP_SIZE = 256u;
static const uint INDIRECT_DRAW_COMMAND_UINTS = 5u;

struct ListCompactionPassConstants
{
    uint candidates_srv;
    uint segments_srv;
    uint visibility_srv;
    uint predicate_mask;
    uint candidate_count;
    uint group_count;
    uint scan_uav;
    uint arguments_uav;
    uint counts_uav;
    uint group_prefix_base;
    uint _pad0;
    uint _pad1;
};

// Mirrors oxygen::vortex::occlusion::internal::GpuIndirectCandidate.
struct IndirectCandidate
{
    uint draw_index;
    uint vertex_count;
    uint instance_count;
    uint segment;
};

// Mirrors oxygen::vortex::occlusion::internal::GpuIndirectSegment.
struct IndirectSegment
{
    uint first_candidate;
    uint candidate_count;
    uint _pad0;
    uint _pad1;
};

groupshared uint gs_scan[2][LIST_COMPACTION_GROUP_SIZE];

ListCompactionPassConstants LoadPassConstants()
{
    StructuredBuffer<ListCompactionPassConstants> constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    return constants[0];
}

// Inclusive Hillis-Steele scan of one value per thread across the group.
uint GroupInclusiveScan(uint value, uint thread_index)
{
    uint source = 0u;
    gs_scan[source][thread_index] = value;
    GroupMemoryBarrierWithGroupSync();
    [unroll]
    for (uint offset = 1u; offset < LIST_COMPACTION_GROUP_SIZE; offset <<= 1u)
    {
        uint sum = gs_scan[source][thread_index];
        if (thread_index >= offset)
        {
            sum += gs_scan[source][thread_index - offset];
        }
        gs_scan[1u - source][thread_index] = sum;
        source = 1u - source;
        GroupMemoryBarrierWithGroupSync();
    }
    return gs_scan[source][thread_index];
}

bool KeepCandidate(ListCompactionPassConstants c, IndirectCandidate candidate)
{
    if (c.predicate_mask == 0u)
    {
        return true;
    }
    StructuredBuffer<uint> visibility = ResourceDescriptorHeap[c.visibility_srv];
    return (visibility[candidate.draw_index] & c.predicate_mask) != 0u;
}

[shader("compute")]
[numthreads(LIST_COMPACTION_GROUP_SIZE, 1, 1)]
void VortexListCompactionScanCS(
    uint3 dispatch_id : SV_DispatchThreadID,
    uint3 group_id : SV_GroupID,
    uint3 thread_id : SV_GroupThreadID)
{
    const ListCompactionPassConstants c = LoadPassConstants();
    const uint index = dispatch_id.x;
    uint keep = 0u;
    if (index < c.candidate_count)
    {
        StructuredBuffer<IndirectCandidate> candidates
            = ResourceDescriptorHeap[c.candidates_srv];
        keep = KeepCandidate(c, candidates[index]) ? 1u : 0u;
    }

    const uint inclusive = GroupInclusiveScan(keep, thread_id.x);
    RWStructuredBuffer<uint> scan = ResourceDescriptorHeap[c.scan_uav];
    if (index < c.candidate_count)
    {
        scan[index] = ((inclusive - keep) << 1u) | keep;
    }
    if (thread_id.x == LIST_COMPACTION_GROUP_SIZE - 1u)
    {
        scan[c.group_prefix_base + group_id.x] = inclusive;
    }
}

[shader("compute")]
[numthreads(LIST_COMPACTION_GROUP_SIZE, 1, 1)]
void VortexListCompactionGroupsCS(uint3 thread_id : SV_GroupThreadID)
{
    const ListCompactionPassConstants c = LoadPassConstants();
    RWStructuredBuffer<uint> scan = ResourceDescriptorHeap[c.scan_uav];

    // Each thread owns a contiguous chunk of group totals, so one group
    // covers any group count.
    const uint chunk = (c.group_count + LIST_COMPACTION_GROUP_SIZE - 1u)
        / LIST_COMPACTION_GROUP_SIZE;
    const uint first = min(thread_id.x * chunk, c.group_count);
    const uint last = min(first + chunk, c.group_count);
    uint chunk_total = 0u;
    for (uint group = first; group < last; ++group)
    {
        chunk_total += scan[c.group_prefix_base + group];
    }

    uint running = GroupInclusiveScan(chunk_total, thread_id.x) - chunk_total;
    for (uint group = first; group < last; ++group)
    {
        const uint total = scan[c.group_prefix_base + group];
        scan[c.group_prefix_base + group] = running;
        running += total;
    }
}

uint GlobalExclusivePrefix(
    ListCompactionPassConstants c, RWStructuredBuffer<uint> scan, uint index)
{
    return (scan[index] >> 1u)
        + scan[c.group_prefix_base + index / LIST_COMPACTION_GROUP_SIZE];
}

[shader("compute")]
[numthreads(LIST_COMPACTION_GROUP_SIZE, 1, 1)]
void VortexListCompactionScatterCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    const ListCompactionPassConstants c = LoadPassConstants();
    const uint index = dispatch_id.x;
    if (index >= c.candidate_count)
    {
        return;
    }

    StructuredBuffer<IndirectCandidate> candidates
        = ResourceDescriptorHeap[c.candidates_srv];
    StructuredBuffer<IndirectSegment> segments
        = ResourceDescriptorHeap[c.segments_srv];
    RWStructuredBuffer<uint> scan = ResourceDescriptorHeap[c.scan_uav];
    RWStructuredBuffer<uint> arguments
        = ResourceDescriptorHeap[c.arguments_uav];

    const IndirectCandidate candidate = candidates[index];
    const IndirectSegment segment = segments[candidate.segment];
    const uint keep = scan[index] & 1u;
    const uint prefix = GlobalExclusivePrefix(c, scan, index);
    const uint segment_prefix
        = GlobalExclusivePrefix(c, scan, segment.first_candidate);
    const uint rank = prefix - segment_prefix;

    if (keep != 0u)
    {
        const uint base
            = (segment.first_candidate + rank) * INDIRECT_DRAW_COMMAND_UINTS;
        arguments[base + 0u] = candidate.draw_index;
        arguments[base + 1u] = candidate.vertex_count;
        arguments[base + 2u] = candidate.instance_count;
        arguments[base + 3u] = 0u;
        arguments[base + 4u] = 0u;
    }

    if (index == segment.first_candidate + segment.candidate_count - 1u)
    {
        RWStructuredBuffer<uint> counts = ResourceDescriptorHeap[c.counts_uav];
        counts[candidate.segment] = rank + keep;
    }
}
