// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json.Serialization;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// DTO for common authored light component values.
/// </summary>
[JsonDerivedType(typeof(DirectionalLightData), "DirectionalLight")]
[JsonDerivedType(typeof(PointLightData), "PointLight")]
[JsonDerivedType(typeof(SpotLightData), "SpotLight")]
public abstract record LightComponentData : ComponentData
{
    /// <summary>Initializes a new instance of the <see cref="LightComponentData"/> class with the creation values of its kind.</summary>
    /// <param name="schema">The scene schema of the light kind.</param>
    private protected LightComponentData(LightSchema schema)
    {
        ArgumentNullException.ThrowIfNull(schema);
        this.AffectsWorld = schema.AffectsWorld;
        this.Color = schema.Color;
        this.CastsShadows = schema.CastsShadows;
        this.Shadow = new LightShadowSettingsData
        {
            Bias = schema.ShadowBias,
            NormalBias = schema.ShadowNormalBias,
            ContactShadows = schema.ContactShadows,
            ResolutionHint = schema.ShadowResolutionHint,
        };
        this.ExposureCompensation = schema.ExposureCompensation;
    }

    /// <summary>
    /// Gets or sets a value indicating whether the light contributes to world lighting.
    /// </summary>
    public bool AffectsWorld { get; set; }

    /// <summary>
    /// Gets or sets the light color multiplier in linear RGB.
    /// </summary>
    public Vector3 Color { get; set; }

    /// <summary>
    /// Gets or sets a value indicating whether the light casts shadows.
    /// </summary>
    public bool CastsShadows { get; set; }

    /// <summary>
    /// Gets or sets the authored shadow settings; absent, the light takes its kind's creation values.
    /// </summary>
    public LightShadowSettingsData? Shadow { get; set; }

    /// <summary>
    /// Gets or sets exposure compensation in EV stops.
    /// </summary>
    public float ExposureCompensation { get; set; }
}
