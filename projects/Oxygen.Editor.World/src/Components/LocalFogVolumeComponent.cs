// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World;

/// <summary>
/// A spherical volume of fog placed by its node's transform. The volume's
/// radius is <see cref="BaseRadiusMeters"/> scaled by the node's world scale.
/// </summary>
public sealed partial class LocalFogVolumeComponent : GameComponent
{
    /// <summary>The volume radius at unit node scale, matching the native component.</summary>
    public const float BaseRadiusMeters = 5f;

    private bool enabled = true;
    private float radialFogExtinction = 1f;
    private float heightFogExtinction = 1f;
    private float heightFogFalloff = 1000f;
    private float heightFogOffset;
    private float fogPhaseG = 0.2f;
    private Vector3 fogAlbedo = Vector3.One;
    private Vector3 fogEmissive;
    private int sortPriority;

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
    public float RadialFogExtinction
    {
        get => this.radialFogExtinction;
        set => _ = this.SetProperty(ref this.radialFogExtinction, value);
    }

    /// <summary>Gets or sets the extinction of the volume's height-based density.</summary>
    public float HeightFogExtinction
    {
        get => this.heightFogExtinction;
        set => _ = this.SetProperty(ref this.heightFogExtinction, value);
    }

    /// <summary>Gets or sets how quickly the height-based density thins above its offset.</summary>
    public float HeightFogFalloff
    {
        get => this.heightFogFalloff;
        set => _ = this.SetProperty(ref this.heightFogFalloff, value);
    }

    /// <summary>Gets or sets the height, relative to the volume center, of the height-based density.</summary>
    public float HeightFogOffset
    {
        get => this.heightFogOffset;
        set => _ = this.SetProperty(ref this.heightFogOffset, value);
    }

    /// <summary>Gets or sets the phase anisotropy; positive values scatter forward.</summary>
    public float FogPhaseG
    {
        get => this.fogPhaseG;
        set => _ = this.SetProperty(ref this.fogPhaseG, value);
    }

    /// <summary>Gets or sets the linear fraction of light the fog scatters rather than absorbs.</summary>
    public Vector3 FogAlbedo
    {
        get => this.fogAlbedo;
        set => _ = this.SetProperty(ref this.fogAlbedo, value);
    }

    /// <summary>Gets or sets the luminance the fog emits.</summary>
    public Vector3 FogEmissive
    {
        get => this.fogEmissive;
        set => _ = this.SetProperty(ref this.fogEmissive, value);
    }

    /// <summary>Gets or sets the order in which overlapping volumes compose, from -127 to 127.</summary>
    public int SortPriority
    {
        get => this.sortPriority;
        set => _ = this.SetProperty(ref this.sortPriority, value);
    }

    /// <inheritdoc/>
    public override void Hydrate(ComponentData data)
    {
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
