// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json.Serialization;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// What a Sky Sphere shows, mirroring Oxygen's native <c>SkySphereSource</c>.
/// </summary>
public enum SkySphereSource
{
    /// <summary>A cooked cube texture.</summary>
    Cubemap = 0,

    /// <summary>A uniform color.</summary>
    SolidColor = 1,
}

/// <summary>
/// Where a Sky Light takes its radiance from, mirroring Oxygen's native <c>SkyLightSource</c>.
/// </summary>
public enum SkyLightSource
{
    /// <summary>The scene's sky, captured by the renderer.</summary>
    CapturedScene = 0,

    /// <summary>An authored cooked cube texture.</summary>
    SpecifiedCubemap = 1,
}

/// <summary>
/// Authored Sky Sphere backdrop, mirrored by Oxygen's native scene <c>SkySphere</c> environment system.
/// </summary>
/// <remarks>
/// Defaults match the native system except <see cref="Enabled"/>, which is off
/// so that scenes without an authored sky sphere show none. An enabled
/// atmosphere or background takes precedence over it.
/// </remarks>
public sealed record SkySphereEnvironmentData
{
    /// <summary>Gets a value indicating whether the sky sphere may show.</summary>
    public bool Enabled { get; init; }

    /// <summary>Gets what the sky sphere shows.</summary>
    public SkySphereSource Source { get; init; } = SkySphereSource.Cubemap;

    /// <summary>Gets the authored cube texture descriptor URI, or null for none.</summary>
    public Uri? Cubemap { get; init; }

    /// <summary>Gets the linear color shown by the solid color source.</summary>
    public Vector3 SolidColorRgb { get; init; }

    /// <summary>
    /// Gets the illuminance, in lux, the sky delivers on an upward-facing surface, or 0 to use the
    /// cubemap or color as imported. The renderer scales the sky's radiance to deliver it, both for
    /// the view and for a captured sky light; <see cref="Intensity"/> and <see cref="TintRgb"/> then
    /// adjust the view only.
    /// </summary>
    public float IlluminanceLux { get; init; }

    /// <summary>Gets the radiance multiplier of the view.</summary>
    public float Intensity { get; init; } = 1.0f;

    /// <summary>Gets the rotation of the cubemap around the up axis, in radians.</summary>
    public float RotationRadians { get; init; }

    /// <summary>Gets the linear tint multiplied into the shown radiance.</summary>
    public Vector3 TintRgb { get; init; } = Vector3.One;

    /// <summary>
    /// Gets the cube texture the renderer uses: the cubemap of an enabled cubemap source, else null.
    /// A cubemap left on a disabled or solid color sky sphere is kept but never loaded or cooked.
    /// </summary>
    [JsonIgnore]
    public Uri? ActiveCubemap => this.Enabled && this.Source == SkySphereSource.Cubemap ? this.Cubemap : null;
}

/// <summary>
/// Authored Sky Light image-based lighting, mirrored by Oxygen's native scene <c>SkyLight</c> environment system.
/// </summary>
/// <remarks>Defaults match the native system.</remarks>
public sealed record SkyLightEnvironmentData
{
    /// <summary>Gets a value indicating whether the sky lights the scene.</summary>
    public bool Enabled { get; init; } = true;

    /// <summary>Gets where the sky light takes its radiance from.</summary>
    public SkyLightSource Source { get; init; } = SkyLightSource.CapturedScene;

    /// <summary>Gets the authored cube texture descriptor URI used by the specified cubemap source, or null for none.</summary>
    public Uri? Cubemap { get; init; }

    /// <summary>
    /// Gets the illuminance, in lux, the specified cubemap delivers on an upward-facing surface, or 0
    /// to use it as imported. Its radiance is scaled to deliver it before the multipliers apply.
    /// </summary>
    public float CubemapIlluminanceLux { get; init; }

    /// <summary>Gets the multiplier applied to both diffuse and specular contributions.</summary>
    public float Intensity { get; init; } = 1.0f;

    /// <summary>Gets the linear tint multiplied into the sky radiance.</summary>
    public Vector3 TintRgb { get; init; } = Vector3.One;

    /// <summary>Gets the diffuse (irradiance) multiplier.</summary>
    public float DiffuseIntensity { get; init; } = 1.0f;

    /// <summary>Gets the specular (reflection) multiplier.</summary>
    public float SpecularIntensity { get; init; } = 1.0f;

    /// <summary>Gets the rotation of the specified cubemap around the up axis, in radians.</summary>
    public float CubemapAngleRadians { get; init; }

    /// <summary>Gets the linear color of the lower hemisphere.</summary>
    public Vector3 LowerHemisphereColor { get; init; }

    /// <summary>Gets a value indicating whether the lower hemisphere is replaced by <see cref="LowerHemisphereColor"/>.</summary>
    public bool LowerHemisphereIsSolidColor { get; init; } = true;

    /// <summary>Gets how much of the lower hemisphere the solid color replaces, from 0 to 1.</summary>
    public float LowerHemisphereBlendAlpha { get; init; } = 1.0f;

    /// <summary>Gets the multiplier of sky light scattered by volumetric fog.</summary>
    public float VolumetricScatteringIntensity { get; init; } = 1.0f;

    /// <summary>Gets a value indicating whether the sky light contributes to reflections.</summary>
    public bool AffectReflections { get; init; } = true;

    /// <summary>
    /// Gets the cube texture the renderer uses: the cubemap of an enabled specified cubemap source,
    /// else null. A cubemap left on a disabled or captured sky light is kept but never loaded or cooked.
    /// </summary>
    [JsonIgnore]
    public Uri? ActiveCubemap => this.Enabled && this.Source == SkyLightSource.SpecifiedCubemap ? this.Cubemap : null;
}

/// <summary>
/// Authored display-only backdrop color, mirrored by Oxygen's native scene <c>Background</c> environment system.
/// </summary>
/// <remarks>
/// The background paints only the presented view: sky light captures and
/// reflections skip it. An enabled atmosphere takes precedence over it.
/// </remarks>
public sealed record BackgroundEnvironmentData
{
    /// <summary>Gets a value indicating whether the background may show.</summary>
    public bool Enabled { get; init; }

    /// <summary>Gets the linear SDR color.</summary>
    public Vector3 ColorRgb { get; init; }
}
