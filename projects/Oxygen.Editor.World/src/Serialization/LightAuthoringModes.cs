// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Serialization;

/// <summary>Explicit atmosphere source assignment, independent of visibility.</summary>
public enum AtmosphereLightSlot
{
    /// <summary>Direct lighting only.</summary>
    None = 0,
    /// <summary>First atmosphere contributor.</summary>
    Primary = 1,
    /// <summary>Second atmosphere contributor.</summary>
    Secondary = 2,
}

/// <summary>
/// Renderer shadow-map resolution hint.
/// </summary>
public enum ShadowResolutionHint
{
    /// <summary>
    /// Low resolution shadow allocation.
    /// </summary>
    Low = 0,

    /// <summary>
    /// Medium resolution shadow allocation.
    /// </summary>
    Medium = 1,

    /// <summary>
    /// High resolution shadow allocation.
    /// </summary>
    High = 2,

    /// <summary>
    /// Ultra resolution shadow allocation.
    /// </summary>
    Ultra = 3,
}

/// <summary>
/// Cascaded shadow split mode for directional lights.
/// </summary>
public enum DirectionalCsmSplitMode
{
    /// <summary>
    /// Generate cascade splits from the max distance and distribution exponent.
    /// </summary>
    Generated = 0,

    /// <summary>
    /// Use the authored cascade distance values directly.
    /// </summary>
    ManualDistances = 1,
}
