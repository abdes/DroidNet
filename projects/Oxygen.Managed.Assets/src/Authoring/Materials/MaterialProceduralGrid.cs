// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Managed.Assets.Authoring.Materials;

/// <summary>
/// How <c>parameters.ambient_occlusion</c> combines with an ambient occlusion texture, as the engine's material
/// descriptor names it.
/// </summary>
public enum MaterialAmbientOcclusionMode
{
    /// <summary>The value multiplies the sample.</summary>
    Factor,

    /// <summary>The value blends from unoccluded to the sample; without a texture the surface is unoccluded.</summary>
    Strength,
}

/// <summary>
/// A world-space grid a material shades any surface with; a material that has one shows it. A value left
/// <see langword="null"/> is not written, and takes the engine's default.
/// </summary>
public sealed record MaterialProceduralGrid
{
    /// <summary>Gets the world units between minor lines, along each axis of a plane.</summary>
    public Vector2? Spacing { get; init; }

    /// <summary>Gets the minor cells per major line.</summary>
    public uint? MajorEvery { get; init; }

    /// <summary>Gets the thickness of minor lines.</summary>
    public float? LineThickness { get; init; }

    /// <summary>Gets the thickness of major lines.</summary>
    public float? MajorThickness { get; init; }

    /// <summary>Gets the thickness of the axis lines.</summary>
    public float? AxisThickness { get; init; }

    /// <summary>Gets the view distance at which the grid starts to fade.</summary>
    public float? FadeStart { get; init; }

    /// <summary>Gets the view distance at which the grid is gone; it does not fade when this is not past <see cref="FadeStart"/>.</summary>
    public float? FadeEnd { get; init; }

    /// <summary>Gets the color of minor lines.</summary>
    public Vector4? MinorColor { get; init; }

    /// <summary>Gets the color of major lines.</summary>
    public Vector4? MajorColor { get; init; }

    /// <summary>Gets the color of the X axis line.</summary>
    public Vector4? AxisColorX { get; init; }

    /// <summary>Gets the color of the Y axis line.</summary>
    public Vector4? AxisColorY { get; init; }

    /// <summary>Gets the color at the origin.</summary>
    public Vector4? OriginColor { get; init; }
}
