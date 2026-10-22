// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json.Serialization;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Scene-level environment authoring data.
/// </summary>
[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
public sealed record SceneEnvironmentData
{
    /// <summary>
    /// Gets or sets a value indicating whether atmosphere rendering is enabled.
    /// </summary>
    public bool AtmosphereEnabled { get; set; } = true;

    /// <summary>
    /// Gets or sets authored sky atmosphere parameters mirrored by the native scene descriptor.
    /// </summary>
    public SkyAtmosphereEnvironmentData SkyAtmosphere { get; set; } = new();

    /// <summary>
    /// Gets or sets authored post-process parameters mirrored from Oxygen's native PostProcessVolume.
    /// </summary>
    public PostProcessEnvironmentData PostProcess { get; set; } = new();

    /// <summary>
    /// Gets or sets authored height fog and volumetric fog mirrored by the native scene descriptor.
    /// </summary>
    public FogEnvironmentData Fog { get; set; } = new();

    /// <summary>
    /// Gets or sets the authored Sky Sphere backdrop mirrored by the native scene descriptor.
    /// </summary>
    public SkySphereEnvironmentData SkySphere { get; set; } = new();

    /// <summary>
    /// Gets or sets the authored Sky Light image-based lighting mirrored by the native scene descriptor.
    /// </summary>
    public SkyLightEnvironmentData SkyLight { get; set; } = new();

    /// <summary>
    /// Gets or sets the authored display-only background mirrored by the native scene descriptor.
    /// </summary>
    public BackgroundEnvironmentData Background { get; set; } = new();

    /// <summary>
    /// Gets the background color of a scene saved before <see cref="Background"/> had its
    /// own enable flag. Loading migrates it into an enabled <see cref="Background"/>; it
    /// is never written.
    /// </summary>
    [JsonPropertyName("BackgroundColor")]
    public Vector3? LegacyBackgroundColor { get; init; }
}
