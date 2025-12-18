// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Data;

namespace Oxygen.Editor.WorldEditor.Inspector;

/// <summary>
/// Converts a <see cref="Oxygen.Editor.World.Components.GameComponent"/> instance (or its type)
/// to a glyph string for use with <c>Segoe Fluent Icons</c>.
/// </summary>
public sealed partial class ComponentToGlyphConverter : IValueConverter
{
    private const string UnknownComponentGlyph = "\uE9CE";

    /// <inheritdoc />
    public object Convert(object value, Type targetType, object parameter, string language)
        => value is null
            ? UnknownComponentGlyph
            : value.GetType().Name switch
            {
                "TransformComponent" => "\uE7AD",
                "MeshComponent" or "GeometryComponent" => "\uF158",
                "MaterialComponent" or "ShaderComponent" => "\uE8B9",
                "PerspectiveCamera" => "\uE714",
                "OrthographicCamera" => "\uE714",
                _ => UnknownComponentGlyph,
            };

    /// <inheritdoc />
    public object ConvertBack(object value, Type targetType, object parameter, string language) =>
        throw new NotSupportedException();
}
