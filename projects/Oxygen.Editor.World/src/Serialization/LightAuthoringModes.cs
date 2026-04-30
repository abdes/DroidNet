// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Runtime participation mode for authored lights.
/// </summary>
public enum LightMobility
{
    /// <summary>
    /// The light is evaluated dynamically at runtime.
    /// </summary>
    Realtime = 0,

    /// <summary>
    /// The light can mix baked and runtime contribution.
    /// </summary>
    Mixed = 1,

    /// <summary>
    /// The light is authored for baked lighting workflows.
    /// </summary>
    Baked = 2,
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
