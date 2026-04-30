// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World;

/// <summary>
/// Base component for authored light data shared by every light kind.
/// </summary>
public abstract partial class LightComponent : GameComponent
{
    private bool affectsWorld = true;
    private Vector3 color = Vector3.One;
    private LightMobility mobility = LightMobility.Realtime;
    private bool castsShadows;
    private float shadowBias;
    private float shadowNormalBias = 0.02f;
    private bool contactShadows;
    private ShadowResolutionHint shadowResolutionHint = ShadowResolutionHint.Medium;
    private float exposureCompensation;

    /// <summary>
    /// Gets or sets a value indicating whether the light contributes to world lighting.
    /// </summary>
    public bool AffectsWorld
    {
        get => this.affectsWorld;
        set => _ = this.SetProperty(ref this.affectsWorld, value);
    }

    /// <summary>
    /// Gets or sets the light color multiplier in linear RGB.
    /// </summary>
    public Vector3 Color
    {
        get => this.color;
        set => _ = this.SetProperty(ref this.color, value);
    }

    /// <summary>
    /// Gets or sets the runtime participation mode for this light.
    /// </summary>
    public LightMobility Mobility
    {
        get => this.mobility;
        set => _ = this.SetProperty(ref this.mobility, value);
    }

    /// <summary>
    /// Gets or sets a value indicating whether this light casts shadows.
    /// </summary>
    public bool CastsShadows
    {
        get => this.castsShadows;
        set => _ = this.SetProperty(ref this.castsShadows, value);
    }

    /// <summary>
    /// Gets or sets the shadow depth bias.
    /// </summary>
    public float ShadowBias
    {
        get => this.shadowBias;
        set => _ = this.SetProperty(ref this.shadowBias, value);
    }

    /// <summary>
    /// Gets or sets the shadow normal bias.
    /// </summary>
    public float ShadowNormalBias
    {
        get => this.shadowNormalBias;
        set => _ = this.SetProperty(ref this.shadowNormalBias, value);
    }

    /// <summary>
    /// Gets or sets a value indicating whether contact shadows are enabled.
    /// </summary>
    public bool ContactShadows
    {
        get => this.contactShadows;
        set => _ = this.SetProperty(ref this.contactShadows, value);
    }

    /// <summary>
    /// Gets or sets the renderer shadow-map resolution hint.
    /// </summary>
    public ShadowResolutionHint ShadowResolutionHint
    {
        get => this.shadowResolutionHint;
        set => _ = this.SetProperty(ref this.shadowResolutionHint, value);
    }

    /// <summary>
    /// Gets or sets exposure compensation in EV stops.
    /// </summary>
    public float ExposureCompensation
    {
        get => this.exposureCompensation;
        set => _ = this.SetProperty(ref this.exposureCompensation, value);
    }

    /// <inheritdoc/>
    public override void Hydrate(ComponentData data)
    {
        base.Hydrate(data);

        if (data is not LightComponentData light)
        {
            return;
        }

        using (this.SuppressNotifications())
        {
            this.AffectsWorld = light.AffectsWorld;
            this.Color = light.Color;
            this.Mobility = light.Mobility;
            this.CastsShadows = light.CastsShadows;
            this.ShadowBias = light.Shadow?.Bias ?? 0f;
            this.ShadowNormalBias = light.Shadow?.NormalBias ?? 0.02f;
            this.ContactShadows = light.Shadow?.ContactShadows ?? false;
            this.ShadowResolutionHint = light.Shadow?.ResolutionHint ?? ShadowResolutionHint.Medium;
            this.ExposureCompensation = light.ExposureCompensation;
        }
    }

    /// <summary>
    /// Creates the serialized common shadow settings for this light.
    /// </summary>
    /// <returns>The authored shadow settings.</returns>
    protected LightShadowSettingsData DehydrateShadow()
        => new()
        {
            Bias = this.ShadowBias,
            NormalBias = this.ShadowNormalBias,
            ContactShadows = this.ContactShadows,
            ResolutionHint = this.ShadowResolutionHint,
        };
}
