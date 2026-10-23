// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// Base component for authored light data shared by every light kind.
/// </summary>
public abstract class LightComponent : GameComponent
{
    private readonly LightSchema schema;
    private bool affectsWorld;
    private Vector3 color;
    private bool castsShadows;
    private float shadowBias;
    private float shadowNormalBias;
    private bool contactShadows;
    private ShadowResolutionHint shadowResolutionHint;
    private float exposureCompensation;

    /// <summary>Initializes a new instance of the <see cref="LightComponent"/> class with the creation values of its kind.</summary>
    /// <param name="schema">The scene schema of the light kind.</param>
    private protected LightComponent(LightSchema schema)
    {
        this.schema = schema;
        this.affectsWorld = schema.AffectsWorld;
        this.color = schema.Color;
        this.castsShadows = schema.CastsShadows;
        this.shadowBias = schema.ShadowBias;
        this.shadowNormalBias = schema.ShadowNormalBias;
        this.contactShadows = schema.ContactShadows;
        this.shadowResolutionHint = schema.ShadowResolutionHint;
        this.exposureCompensation = schema.ExposureCompensation;
    }

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
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public Vector3 Color
    {
        get => this.color;
        set
        {
            PropertyChecks.Require(this.schema.ColorId, value);
            _ = this.SetProperty(ref this.color, value);
        }
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
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float ShadowBias
    {
        get => this.shadowBias;
        set
        {
            PropertyChecks.Require(this.schema.ShadowBiasId, value);
            _ = this.SetProperty(ref this.shadowBias, value);
        }
    }

    /// <summary>
    /// Gets or sets the shadow normal bias.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float ShadowNormalBias
    {
        get => this.shadowNormalBias;
        set
        {
            PropertyChecks.Require(this.schema.ShadowNormalBiasId, value);
            _ = this.SetProperty(ref this.shadowNormalBias, value);
        }
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
    /// <exception cref="ArgumentOutOfRangeException">The value is not a member the scene schema declares.</exception>
    public ShadowResolutionHint ShadowResolutionHint
    {
        get => this.shadowResolutionHint;
        set
        {
            PropertyChecks.Require(this.schema.ShadowResolutionHintId, value);
            _ = this.SetProperty(ref this.shadowResolutionHint, value);
        }
    }

    /// <summary>
    /// Gets or sets exposure compensation in EV stops.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float ExposureCompensation
    {
        get => this.exposureCompensation;
        set
        {
            PropertyChecks.Require(this.schema.ExposureCompensationId, value);
            _ = this.SetProperty(ref this.exposureCompensation, value);
        }
    }

    /// <inheritdoc/>
    /// <remarks>A light kind checks its own values before the shared ones, so the light rules see only values in range.</remarks>
    /// <exception cref="ArgumentOutOfRangeException">A value is outside the range the scene schema declares; nothing changes.</exception>
    /// <exception cref="ArgumentException">The values break a light rule (<see cref="LightValidation"/>); nothing changes.</exception>
    public override void Hydrate(ComponentData data)
    {
        if (data is LightComponentData candidate)
        {
            var shadow = this.ShadowOf(candidate);
            PropertyChecks.Require(this.schema.ColorId, candidate.Color);
            PropertyChecks.Require(this.schema.ExposureCompensationId, candidate.ExposureCompensation);
            PropertyChecks.Require(this.schema.ShadowBiasId, shadow.Bias);
            PropertyChecks.Require(this.schema.ShadowNormalBiasId, shadow.NormalBias);
            PropertyChecks.Require(this.schema.ShadowResolutionHintId, shadow.ResolutionHint);
            if (LightValidation.Validate(candidate) is { } violation)
            {
                throw new ArgumentException(violation.Message, nameof(data));
            }
        }

        base.Hydrate(data);

        if (data is not LightComponentData light)
        {
            return;
        }

        var settings = this.ShadowOf(light);
        using (this.SuppressNotifications())
        {
            this.AffectsWorld = light.AffectsWorld;
            this.Color = light.Color;

            this.CastsShadows = light.CastsShadows;
            this.ShadowBias = settings.Bias;
            this.ShadowNormalBias = settings.NormalBias;
            this.ContactShadows = settings.ContactShadows;
            this.ShadowResolutionHint = settings.ResolutionHint;
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

    // Absent shadow settings are the kind's creation values.
    private LightShadowSettingsData ShadowOf(LightComponentData light)
        => light.Shadow ?? new()
        {
            Bias = this.schema.ShadowBias,
            NormalBias = this.schema.ShadowNormalBias,
            ContactShadows = this.schema.ContactShadows,
            ResolutionHint = this.schema.ShadowResolutionHint,
        };
}
