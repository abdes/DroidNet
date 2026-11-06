// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Materials.Properties;

/// <summary>The properties of a material's procedural grid, each the JSON Pointer of its field.</summary>
public static class MaterialGridProperties
{
    /// <summary>Gets the spacing along the first axis of a plane.</summary>
    public static PropertyId<float> SpacingX { get; } = new(MaterialDescriptors.Kind, "/procedural_grid/spacing/0");

    /// <summary>Gets the spacing along the second axis of a plane.</summary>
    public static PropertyId<float> SpacingY { get; } = new(MaterialDescriptors.Kind, "/procedural_grid/spacing/1");

    /// <summary>Gets the minor cells per major line.</summary>
    public static PropertyId<uint> MajorEvery { get; } = new(MaterialDescriptors.Kind, "/procedural_grid/major_every");

    /// <summary>Gets the width of minor lines.</summary>
    public static PropertyId<float> LineThickness { get; } = new(MaterialDescriptors.Kind, "/procedural_grid/line_thickness");

    /// <summary>Gets the width of major lines.</summary>
    public static PropertyId<float> MajorThickness { get; } = new(MaterialDescriptors.Kind, "/procedural_grid/major_thickness");

    /// <summary>Gets the width of the axis lines.</summary>
    public static PropertyId<float> AxisThickness { get; } = new(MaterialDescriptors.Kind, "/procedural_grid/axis_thickness");

    /// <summary>Gets the view distance at which the grid starts to fade.</summary>
    public static PropertyId<float> FadeStart { get; } = new(MaterialDescriptors.Kind, "/procedural_grid/fade_start");

    /// <summary>Gets the view distance at which the grid is gone.</summary>
    public static PropertyId<float> FadeEnd { get; } = new(MaterialDescriptors.Kind, "/procedural_grid/fade_end");

    /// <summary>Gets the color of minor lines.</summary>
    public static MaterialColorProperties MinorColor { get; } = new("minor_color");

    /// <summary>Gets the color of major lines.</summary>
    public static MaterialColorProperties MajorColor { get; } = new("major_color");

    /// <summary>Gets the color of the X axis line.</summary>
    public static MaterialColorProperties AxisColorX { get; } = new("axis_color_x");

    /// <summary>Gets the color of the Y axis line.</summary>
    public static MaterialColorProperties AxisColorY { get; } = new("axis_color_y");

    /// <summary>Gets the color at the origin.</summary>
    public static MaterialColorProperties OriginColor { get; } = new("origin_color");
}

/// <summary>The channels of one of the procedural grid's colors.</summary>
/// <param name="Field">The color's field in the grid.</param>
public sealed record MaterialColorProperties(string Field)
{
    /// <summary>Gets the channels: red, green, blue and alpha.</summary>
    public IReadOnlyList<PropertyId<float>> Channels { get; } =
    [
        new(MaterialDescriptors.Kind, $"/procedural_grid/{Field}/0"),
        new(MaterialDescriptors.Kind, $"/procedural_grid/{Field}/1"),
        new(MaterialDescriptors.Kind, $"/procedural_grid/{Field}/2"),
        new(MaterialDescriptors.Kind, $"/procedural_grid/{Field}/3"),
    ];
}
