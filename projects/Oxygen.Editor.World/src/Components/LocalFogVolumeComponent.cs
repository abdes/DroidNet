// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// A spherical volume of fog placed by its node's transform. The volume's
/// radius is <see cref="BaseRadiusMeters"/> scaled by the node's world scale.
/// </summary>
public sealed partial class LocalFogVolumeComponent : GameComponent
{
    /// <summary>The volume radius at unit node scale, matching the native component.</summary>
    public const float BaseRadiusMeters = 5f;

    private bool enabled = SceneSchema.LocalFogVolume.Defaults.Enabled;
    private float radialFogExtinction = SceneSchema.LocalFogVolume.Defaults.RadialFogExtinction;
    private float heightFogExtinction = SceneSchema.LocalFogVolume.Defaults.HeightFogExtinction;
    private float heightFogFalloff = SceneSchema.LocalFogVolume.Defaults.HeightFogFalloff;
    private float heightFogOffset = SceneSchema.LocalFogVolume.Defaults.HeightFogOffset;
    private float fogPhaseG = SceneSchema.LocalFogVolume.Defaults.FogPhaseG;
    private Vector3 fogAlbedo = SceneSchema.LocalFogVolume.Defaults.FogAlbedo;
    private Vector3 fogEmissive = SceneSchema.LocalFogVolume.Defaults.FogEmissive;
    private int sortPriority = SceneSchema.LocalFogVolume.Defaults.SortPriority;

    static LocalFogVolumeComponent()
    {
        Register<LocalFogVolumeData>(d =>
        {
            var c = new LocalFogVolumeComponent { Name = d.Name };
            c.Hydrate(d);
            return c;
        });
    }

    /// <summary>Gets or sets a value indicating whether the volume renders.</summary>
    public bool Enabled
    {
        get => this.enabled;
        set => _ = this.SetProperty(ref this.enabled, value);
    }

    /// <summary>Gets or sets the fog density: extinction per metre at the volume's center, fading to zero at its edge.</summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float RadialFogExtinction
    {
        get => this.radialFogExtinction;
        set
        {
            PropertyChecks.Require(SceneSchema.LocalFogVolume.RadialFogExtinction, value);
            _ = this.SetProperty(ref this.radialFogExtinction, value);
        }
    }

    /// <summary>Gets or sets the extinction of the volume's height-based density.</summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float HeightFogExtinction
    {
        get => this.heightFogExtinction;
        set
        {
            PropertyChecks.Require(SceneSchema.LocalFogVolume.HeightFogExtinction, value);
            _ = this.SetProperty(ref this.heightFogExtinction, value);
        }
    }

    /// <summary>Gets or sets how quickly the height-based density thins above its offset.</summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float HeightFogFalloff
    {
        get => this.heightFogFalloff;
        set
        {
            PropertyChecks.Require(SceneSchema.LocalFogVolume.HeightFogFalloff, value);
            _ = this.SetProperty(ref this.heightFogFalloff, value);
        }
    }

    /// <summary>Gets or sets the height, relative to the volume center, of the height-based density.</summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float HeightFogOffset
    {
        get => this.heightFogOffset;
        set
        {
            PropertyChecks.Require(SceneSchema.LocalFogVolume.HeightFogOffset, value);
            _ = this.SetProperty(ref this.heightFogOffset, value);
        }
    }

    /// <summary>Gets or sets the phase anisotropy; positive values scatter forward.</summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float FogPhaseG
    {
        get => this.fogPhaseG;
        set
        {
            PropertyChecks.Require(SceneSchema.LocalFogVolume.FogPhaseG, value);
            _ = this.SetProperty(ref this.fogPhaseG, value);
        }
    }

    /// <summary>Gets or sets the linear fraction of light the fog scatters rather than absorbs.</summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public Vector3 FogAlbedo
    {
        get => this.fogAlbedo;
        set
        {
            PropertyChecks.Require(SceneSchema.LocalFogVolume.FogAlbedo, value);
            _ = this.SetProperty(ref this.fogAlbedo, value);
        }
    }

    /// <summary>Gets or sets the luminance the fog emits.</summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public Vector3 FogEmissive
    {
        get => this.fogEmissive;
        set
        {
            PropertyChecks.Require(SceneSchema.LocalFogVolume.FogEmissive, value);
            _ = this.SetProperty(ref this.fogEmissive, value);
        }
    }

    /// <summary>Gets or sets the order in which overlapping volumes compose, from -127 to 127.</summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public int SortPriority
    {
        get => this.sortPriority;
        set
        {
            PropertyChecks.Require(SceneSchema.LocalFogVolume.SortPriority, value);
            _ = this.SetProperty(ref this.sortPriority, value);
        }
    }

    /// <inheritdoc/>
    /// <exception cref="ArgumentOutOfRangeException">A value is outside the range the scene schema declares; nothing changes.</exception>
    public override void Hydrate(ComponentData data)
    {
        if (data is LocalFogVolumeData candidate)
        {
            PropertyChecks.Require(SceneSchema.LocalFogVolume.RadialFogExtinction, candidate.RadialFogExtinction);
            PropertyChecks.Require(SceneSchema.LocalFogVolume.HeightFogExtinction, candidate.HeightFogExtinction);
            PropertyChecks.Require(SceneSchema.LocalFogVolume.HeightFogFalloff, candidate.HeightFogFalloff);
            PropertyChecks.Require(SceneSchema.LocalFogVolume.HeightFogOffset, candidate.HeightFogOffset);
            PropertyChecks.Require(SceneSchema.LocalFogVolume.FogPhaseG, candidate.FogPhaseG);
            PropertyChecks.Require(SceneSchema.LocalFogVolume.FogAlbedo, candidate.FogAlbedo);
            PropertyChecks.Require(SceneSchema.LocalFogVolume.FogEmissive, candidate.FogEmissive);
            PropertyChecks.Require(SceneSchema.LocalFogVolume.SortPriority, candidate.SortPriority);
        }

        base.Hydrate(data);
        if (data is not LocalFogVolumeData volume)
        {
            return;
        }

        using (this.SuppressNotifications())
        {
            this.Enabled = volume.Enabled;
            this.RadialFogExtinction = volume.RadialFogExtinction;
            this.HeightFogExtinction = volume.HeightFogExtinction;
            this.HeightFogFalloff = volume.HeightFogFalloff;
            this.HeightFogOffset = volume.HeightFogOffset;
            this.FogPhaseG = volume.FogPhaseG;
            this.FogAlbedo = volume.FogAlbedo;
            this.FogEmissive = volume.FogEmissive;
            this.SortPriority = volume.SortPriority;
        }
    }

    /// <inheritdoc/>
    public override ComponentData Dehydrate()
        => new LocalFogVolumeData
        {
            Id = this.Id,
            Name = this.Name,
            Enabled = this.Enabled,
            RadialFogExtinction = this.RadialFogExtinction,
            HeightFogExtinction = this.HeightFogExtinction,
            HeightFogFalloff = this.HeightFogFalloff,
            HeightFogOffset = this.HeightFogOffset,
            FogPhaseG = this.FogPhaseG,
            FogAlbedo = this.FogAlbedo,
            FogEmissive = this.FogEmissive,
            SortPriority = this.SortPriority,
        };
}
