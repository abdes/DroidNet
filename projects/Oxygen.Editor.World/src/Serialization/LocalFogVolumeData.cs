// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json.Serialization;

namespace Oxygen.Editor.World.Serialization;

/// <summary>Persisted state of a local fog volume component; defaults match the native component.</summary>
[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
public sealed record LocalFogVolumeData : ComponentData
{
    /// <summary>Gets a value indicating whether the volume renders.</summary>
    public bool Enabled { get; init; } = true;

    /// <summary>Gets the extinction at the volume's center.</summary>
    public float RadialFogExtinction { get; init; } = 1f;

    /// <summary>Gets the extinction of the volume's height-based density.</summary>
    public float HeightFogExtinction { get; init; } = 1f;

    /// <summary>Gets how quickly the height-based density thins above its offset.</summary>
    public float HeightFogFalloff { get; init; } = 1000f;

    /// <summary>Gets the height of the height-based density relative to the volume center.</summary>
    public float HeightFogOffset { get; init; }

    /// <summary>Gets the phase anisotropy.</summary>
    public float FogPhaseG { get; init; } = 0.2f;

    /// <summary>Gets the linear fog albedo.</summary>
    public Vector3 FogAlbedo { get; init; } = Vector3.One;

    /// <summary>Gets the emitted luminance.</summary>
    public Vector3 FogEmissive { get; init; }

    /// <summary>Gets the composition order among overlapping volumes.</summary>
    public int SortPriority { get; init; }
}
