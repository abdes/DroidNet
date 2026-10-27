// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Components.Geometry;

/// <summary>The labels of a surface's rows, as the inspector names them in summaries and tooltips.</summary>
internal static class SurfaceLabels
{
    /// <summary>Gets the surface properties in row order, with their labels.</summary>
    public static IReadOnlyList<(PropertyId Property, string Label)> Rows { get; } =
    [
        (SceneSchema.Geometry.Surface.Material.Id, "Material"),
        (SceneSchema.Geometry.Surface.Visibility.Id, "Visible"),
        (SceneSchema.Geometry.Surface.CastsShadows.Id, "Cast shadows"),
    ];

    /// <summary>Gets the label of a surface property.</summary>
    /// <param name="property">A property of the surface part.</param>
    /// <returns>The row's label.</returns>
    public static string Of(PropertyId property)
    {
        foreach (var (prop, label) in Rows)
        {
            if (prop == property)
            {
                return label;
            }
        }

        throw new ArgumentOutOfRangeException(nameof(property), property, "The surface has no row for this property.");
    }
}
