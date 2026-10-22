// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Authored sky atmosphere parameters that map to Oxygen's native sky atmosphere environment descriptor.
/// </summary>
public sealed record SkyAtmosphereEnvironmentData
{
    /// <summary>
    /// Gets or sets planet radius in meters.
    /// </summary>
    public float PlanetRadiusMeters { get; set; } = 6_360_000.0f;

    /// <summary>
    /// Gets or sets atmosphere height in meters.
    /// </summary>
    public float AtmosphereHeightMeters { get; set; } = 100_000.0f;

    /// <summary>
    /// Gets or sets linear RGB ground albedo.
    /// </summary>
    public Vector3 GroundAlbedoRgb { get; set; } = new(0.4f, 0.4f, 0.4f);

    /// <summary>
    /// Gets or sets Rayleigh scattering scale height in meters.
    /// </summary>
    public float RayleighScaleHeightMeters { get; set; } = 8_000.0f;

    /// <summary>
    /// Gets or sets Mie scattering scale height in meters.
    /// </summary>
    public float MieScaleHeightMeters { get; set; } = 1_200.0f;

    /// <summary>
    /// Gets or sets Mie anisotropy g.
    /// </summary>
    public float MieAnisotropy { get; set; } = 0.8f;

    /// <summary>
    /// Gets or sets sky luminance multiplier.
    /// </summary>
    public Vector3 SkyLuminanceFactorRgb { get; set; } = Vector3.One;

    /// <summary>
    /// Gets or sets aerial perspective distance scale.
    /// </summary>
    public float AerialPerspectiveDistanceScale { get; set; } = 1.0f;

    /// <summary>
    /// Gets or sets aerial perspective scattering strength.
    /// </summary>
    public float AerialScatteringStrength { get; set; } = 1.0f;

    /// <summary>
    /// Gets or sets aerial perspective start depth in meters.
    /// </summary>
    public float AerialPerspectiveStartDepthMeters { get; set; } = 100.0f;

    /// <summary>
    /// Gets or sets height fog contribution.
    /// </summary>
    public float HeightFogContribution { get; set; } = 1.0f;

    /// <summary>
    /// Gets or sets a value indicating whether the sun disk is rendered by the sky model.
    /// </summary>
    public bool SunDiskEnabled { get; set; } = true;
}
