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
    public required float Bias { get; init; }

    /// <summary>
    /// Gets or sets the shadow normal bias.
    /// </summary>
    public required float NormalBias { get; set; }

    /// <summary>
    /// Gets a value indicating whether contact shadows are enabled.
    /// </summary>
    public required bool ContactShadows { get; init; }

    /// <summary>
    /// Gets or sets the renderer shadow-map resolution hint.
    /// </summary>
    public required ShadowResolutionHint ResolutionHint { get; set; }
}
