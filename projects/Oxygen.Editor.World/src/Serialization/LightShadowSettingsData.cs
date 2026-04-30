// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// DTO for authored shadow settings shared by all light types.
/// </summary>
public sealed record LightShadowSettingsData
{
    /// <summary>
    /// Gets the shadow depth bias.
    /// </summary>
    public float Bias { get; init; }

    /// <summary>
    /// Gets the shadow normal bias.
    /// </summary>
    public float NormalBias { get; init; } = 0.02f;

    /// <summary>
    /// Gets a value indicating whether contact shadows are enabled.
    /// </summary>
    public bool ContactShadows { get; init; }

    /// <summary>
    /// Gets the renderer shadow-map resolution hint.
    /// </summary>
    public ShadowResolutionHint ResolutionHint { get; init; } = ShadowResolutionHint.Medium;
}
