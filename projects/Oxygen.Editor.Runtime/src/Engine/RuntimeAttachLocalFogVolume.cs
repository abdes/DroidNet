// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Immutable runtime request to attach or replace a node's local fog volume.</summary>
/// <param name="NodeId">The node that carries the volume.</param>
/// <param name="Enabled">Whether the volume renders.</param>
/// <param name="RadialFogExtinction">The extinction at the volume's center.</param>
/// <param name="HeightFogExtinction">The extinction of the height-based density.</param>
/// <param name="HeightFogFalloff">How quickly the height-based density thins.</param>
/// <param name="HeightFogOffset">The height of the height-based density relative to the center.</param>
/// <param name="FogPhaseG">The phase anisotropy.</param>
/// <param name="FogAlbedo">The linear fog albedo.</param>
/// <param name="FogEmissive">The emitted luminance.</param>
/// <param name="SortPriority">The composition order among overlapping volumes.</param>
public sealed record RuntimeAttachLocalFogVolume(
    Guid NodeId,
    bool Enabled,
    float RadialFogExtinction,
    float HeightFogExtinction,
    float HeightFogFalloff,
    float HeightFogOffset,
    float FogPhaseG,
    Vector3 FogAlbedo,
    Vector3 FogEmissive,
    int SortPriority) : RuntimeWorldCommand;
