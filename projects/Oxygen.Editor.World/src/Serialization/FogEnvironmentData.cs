// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Authored exponential height fog and volumetric fog, mirrored by Oxygen's
/// native scene <c>Fog</c> environment system.
/// </summary>
/// <remarks>
/// Defaults match the native system except <see cref="Enabled"/>, which is
/// off so that scenes without authored fog render none.
/// </remarks>
public sealed record FogEnvironmentData
{
    /// <summary>Gets a value indicating whether the scene renders fog.</summary>
    public bool Enabled { get; init; }

    /// <summary>Gets or sets a value indicating whether the exponential height fog layers render.</summary>
    public bool HeightFogEnabled { get; set; } = true;

    /// <summary>Gets or sets the first layer's extinction coefficient at its height offset, per metre.</summary>
    public float Density { get; set; } = 0.002f;

    /// <summary>Gets or sets how quickly the first layer thins with height, per metre.</summary>
    public float HeightFalloff { get; set; } = 0.02f;

    /// <summary>Gets the world height at which the first layer has its authored density.</summary>
    public float HeightOffsetMeters { get; init; }

    /// <summary>Gets or sets the largest fraction of the scene the fog may hide.</summary>
    public float MaxOpacity { get; set; } = 1.0f;

    /// <summary>Gets the fog's own inscattered luminance.</summary>
    public Vector3 InscatteringLuminanceRgb { get; init; }

    /// <summary>Gets or sets the scale applied to ambient light the sky atmosphere contributes to the fog.</summary>
    public Vector3 SkyAmbientScaleRgb { get; set; } = Vector3.One;

    /// <summary>Gets the second layer's extinction coefficient at its height offset, per metre.</summary>
    public float SecondDensity { get; init; }

    /// <summary>Gets how quickly the second layer thins with height, per metre.</summary>
    public float SecondHeightFalloff { get; init; }

    /// <summary>Gets the world height at which the second layer has its authored density.</summary>
    public float SecondHeightOffsetMeters { get; init; }

    /// <summary>Gets the camera distance at which fog begins.</summary>
    public float StartDistanceMeters { get; init; }

    /// <summary>Gets the horizontal distance beyond which fog stops accumulating; 0 is unlimited.</summary>
    public float EndDistanceMeters { get; init; }

    /// <summary>Gets the distance beyond which surfaces receive no fog; 0 is unlimited.</summary>
    public float CutoffDistanceMeters { get; init; }

    /// <summary>Gets the inscattered luminance of the lobe toward the primary atmosphere light.</summary>
    public Vector3 DirectionalInscatteringLuminanceRgb { get; init; }

    /// <summary>Gets or sets the sharpness of the lobe toward the primary atmosphere light.</summary>
    public float DirectionalInscatteringExponent { get; set; } = 4.0f;

    /// <summary>Gets or sets the camera distance at which the directional lobe begins.</summary>
    public float DirectionalInscatteringStartDistanceMeters { get; set; } = 10000.0f;

    /// <summary>Gets a value indicating whether lights scatter through a volumetric fog grid.</summary>
    public bool VolumetricFogEnabled { get; init; }

    /// <summary>Gets the phase anisotropy; positive values scatter forward.</summary>
    public float VolumetricScatteringDistribution { get; init; }

    /// <summary>Gets or sets the fraction of light the volumetric fog scatters rather than absorbs.</summary>
    public Vector3 VolumetricAlbedoRgb { get; set; } = Vector3.One;

    /// <summary>Gets the luminance the volumetric fog emits.</summary>
    public Vector3 VolumetricEmissiveRgb { get; init; }

    /// <summary>Gets or sets the scale applied to height fog extinction inside the volumetric grid.</summary>
    public float VolumetricExtinctionScale { get; set; } = 1.0f;

    /// <summary>Gets the far end of the volumetric grid; 0 uses the camera far plane.</summary>
    public float VolumetricDistanceMeters { get; init; }

    /// <summary>Gets the near end of the volumetric grid.</summary>
    public float VolumetricStartDistanceMeters { get; init; }

    /// <summary>Gets the distance over which volumetric fog fades in from its start.</summary>
    public float VolumetricNearFadeInDistanceMeters { get; init; }

    /// <summary>Gets or sets the scale applied to sky lighting scattered by the volumetric fog.</summary>
    public float VolumetricStaticLightingScatteringIntensity { get; set; } = 1.0f;

    /// <summary>Gets a value indicating whether lights scatter with the fog's inscattering colors instead of their own.</summary>
    public bool OverrideLightColorsWithFogInscattering { get; init; }

    /// <summary>Gets or sets a value indicating whether the fog renders in the main scene pass.</summary>
    public bool RenderInMainPass { get; set; } = true;

    /// <summary>Gets a value indicating whether the fog cuts out the scene behind it without adding its own color.</summary>
    public bool Holdout { get; init; }

    /// <summary>Gets or sets a value indicating whether reflection captures include the fog.</summary>
    public bool VisibleInReflectionCaptures { get; set; } = true;

    /// <summary>Gets or sets a value indicating whether sky light captures include the fog.</summary>
    public bool VisibleInRealTimeSkyCaptures { get; set; } = true;
}
