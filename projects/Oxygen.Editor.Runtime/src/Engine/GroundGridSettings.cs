// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
///     How every viewport draws the ground grid. A user preference, never authored data. A new instance holds the
///     engine defaults.
/// </summary>
/// <remarks>
///     The grid lies on the ground plane through the world origin. Its lines are <see cref="Spacing"/> apart, every
///     <see cref="MajorEvery"/>th line is a major line, and the lines through <see cref="OriginX"/>,
///     <see cref="OriginY"/> are the axes. Colors are the RGBA values the grid blends over the image.
/// </remarks>
public sealed record GroundGridSettings
{
    /// <summary>Gets a value indicating whether views draw the grid at all.</summary>
    public bool Enabled { get; init; } = true;

    /// <summary>Gets the distance between adjacent grid lines, in world units.</summary>
    public float Spacing { get; init; } = 1f;

    /// <summary>Gets how many minor cells lie between major lines.</summary>
    public int MajorEvery { get; init; } = 10;

    /// <summary>Gets the minor line thickness, as a fraction of a cell.</summary>
    public float LineThickness { get; init; } = 0.02f;

    /// <summary>Gets the major line thickness, as a fraction of a cell.</summary>
    public float MajorThickness { get; init; } = 0.04f;

    /// <summary>Gets the axis line thickness, as a fraction of a cell.</summary>
    public float AxisThickness { get; init; } = 0.06f;

    /// <summary>Gets the distance from the camera at which the grid starts to fade, in world units.</summary>
    public float FadeStart { get; init; }

    /// <summary>Gets the exponent of the fade with distance.</summary>
    public float FadePower { get; init; } = 2f;

    /// <summary>Gets how much the lines brighten toward the horizon, where they thin out.</summary>
    public float HorizonBoost { get; init; } = 0.35f;

    /// <summary>Gets the X coordinate of the grid origin on the ground plane.</summary>
    public float OriginX { get; init; }

    /// <summary>Gets the Y coordinate of the grid origin on the ground plane.</summary>
    public float OriginY { get; init; }

    /// <summary>Gets a value indicating whether the grid eases after the camera instead of snapping cell by cell.</summary>
    public bool SmoothMotion { get; init; } = true;

    /// <summary>Gets the easing time of smooth motion, in seconds.</summary>
    public float SmoothTime { get; init; } = 1f;

    /// <summary>Gets the minor line color.</summary>
    public RuntimeColor MinorColor { get; init; } = new(0.16f, 0.16f, 0.16f, 1f);

    /// <summary>Gets the major line color.</summary>
    public RuntimeColor MajorColor { get; init; } = new(0.20f, 0.20f, 0.20f, 1f);

    /// <summary>Gets the color of the X axis line.</summary>
    public RuntimeColor AxisColorX { get; init; } = new(0.7f, 0.23f, 0.23f, 1f);

    /// <summary>Gets the color of the Y axis line.</summary>
    public RuntimeColor AxisColorY { get; init; } = new(0.23f, 0.7f, 0.23f, 1f);

    /// <summary>Gets the color of the origin marker.</summary>
    public RuntimeColor OriginColor { get; init; } = new(1f, 1f, 1f, 1f);
}
