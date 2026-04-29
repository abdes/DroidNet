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
    /// Gets planet radius in meters.
    /// </summary>
    public float PlanetRadiusMeters { get; init; } = 6_360_000.0f;

    /// <summary>
    /// Gets atmosphere height in meters.
    /// </summary>
    public float AtmosphereHeightMeters { get; init; } = 80_000.0f;

    /// <summary>
    /// Gets linear RGB ground albedo.
    /// </summary>
    public Vector3 GroundAlbedoRgb { get; init; } = new(0.4f, 0.4f, 0.4f);

    /// <summary>
    /// Gets Rayleigh scattering scale height in meters.
    /// </summary>
    public float RayleighScaleHeightMeters { get; init; } = 8_000.0f;

    /// <summary>
    /// Gets Mie scattering scale height in meters.
    /// </summary>
    public float MieScaleHeightMeters { get; init; } = 1_200.0f;

    /// <summary>
    /// Gets Mie anisotropy g.
    /// </summary>
    public float MieAnisotropy { get; init; } = 0.8f;

    /// <summary>
    /// Gets sky luminance multiplier.
    /// </summary>
    public Vector3 SkyLuminanceFactorRgb { get; init; } = Vector3.One;

    /// <summary>
    /// Gets aerial perspective distance scale.
    /// </summary>
    public float AerialPerspectiveDistanceScale { get; init; } = 1.0f;

    /// <summary>
    /// Gets aerial perspective scattering strength.
    /// </summary>
    public float AerialScatteringStrength { get; init; } = 1.0f;

    /// <summary>
    /// Gets aerial perspective start depth in meters.
    /// </summary>
    public float AerialPerspectiveStartDepthMeters { get; init; }

    /// <summary>
    /// Gets height fog contribution.
    /// </summary>
    public float HeightFogContribution { get; init; } = 1.0f;

    /// <summary>
    /// Gets a value indicating whether the sun disk is rendered by the sky model.
    /// </summary>
    public bool SunDiskEnabled { get; init; } = true;
}
