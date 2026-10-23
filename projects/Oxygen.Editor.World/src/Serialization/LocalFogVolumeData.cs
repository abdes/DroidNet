// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json.Serialization;

namespace Oxygen.Editor.World.Serialization;

/// <summary>Persisted state of a local fog volume component; defaults come from the scene schema.</summary>
[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
public sealed record LocalFogVolumeData : ComponentData
{
    /// <summary>Gets or sets a value indicating whether the volume renders.</summary>
    public bool Enabled { get; set; } = SceneSchema.LocalFogVolume.Defaults.Enabled;

    /// <summary>Gets or sets the extinction at the volume's center.</summary>
    public float RadialFogExtinction { get; set; } = SceneSchema.LocalFogVolume.Defaults.RadialFogExtinction;

    /// <summary>Gets or sets the extinction of the volume's height-based density.</summary>
    public float HeightFogExtinction { get; set; } = SceneSchema.LocalFogVolume.Defaults.HeightFogExtinction;

    /// <summary>Gets or sets how quickly the height-based density thins above its offset.</summary>
    public float HeightFogFalloff { get; set; } = SceneSchema.LocalFogVolume.Defaults.HeightFogFalloff;

    /// <summary>Gets or sets the height of the height-based density relative to the volume center.</summary>
    public float HeightFogOffset { get; set; } = SceneSchema.LocalFogVolume.Defaults.HeightFogOffset;

    /// <summary>Gets or sets the phase anisotropy.</summary>
    public float FogPhaseG { get; set; } = SceneSchema.LocalFogVolume.Defaults.FogPhaseG;

    /// <summary>Gets or sets the linear fog albedo.</summary>
    public Vector3 FogAlbedo { get; set; } = SceneSchema.LocalFogVolume.Defaults.FogAlbedo;

    /// <summary>Gets or sets the emitted luminance.</summary>
    public Vector3 FogEmissive { get; set; } = SceneSchema.LocalFogVolume.Defaults.FogEmissive;

    /// <summary>Gets or sets the composition order among overlapping volumes.</summary>
    public int SortPriority { get; set; } = SceneSchema.LocalFogVolume.Defaults.SortPriority;
}
