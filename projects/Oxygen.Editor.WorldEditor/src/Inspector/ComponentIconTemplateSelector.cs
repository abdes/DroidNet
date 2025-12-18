// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;
using DataTemplate = Microsoft.UI.Xaml.DataTemplate;
using DependencyObject = Microsoft.UI.Xaml.DependencyObject;

namespace Oxygen.Editor.WorldEditor.Inspector;

/// <summary>
/// Selects a thumbnail template for a component row.
/// Use this to render any icon UI (FontIcon, PathIcon, Image, etc.) without using IconSourceElement.
/// </summary>
/// <remarks>
/// The selector determines which template to return by inspecting the runtime type name of the
/// provided data item (for example: "TransformComponent", "MeshComponent", "GeometryComponent",
/// "PerspectiveCamera" and "OrthographicCamera"). When a specialized template for the detected
/// type is not set (null), the selector falls back to <see cref="DefaultTemplate"/>.
/// </remarks>
public sealed partial class ComponentIconTemplateSelector : DataTemplateSelector
{
    /// <summary>
    /// Gets or sets the default template returned when no specialized template matches the item.
    /// </summary>
    /// <value>
    /// A <see cref="DataTemplate"/> used as a fallback. May be <see langword="null"/>.
    /// </value>
    public DataTemplate? DefaultTemplate { get; set; }

    /// <summary>
    /// Gets or sets the template used for transform components (type name "TransformComponent").
    /// </summary>
    /// <value>
    /// A <see cref="DataTemplate"/> specifically for transform components. May be <see langword="null"/>
    /// in which case <see cref="DefaultTemplate"/> will be used.
    /// </value>
    public DataTemplate? TransformTemplate { get; set; }

    /// <summary>
    /// Gets or sets the template used for geometry components (type names "MeshComponent" or
    /// "GeometryComponent").
    /// </summary>
    /// <value>
    /// A <see cref="DataTemplate"/> specifically for geometry components. May be <see langword="null"/>
    /// in which case <see cref="DefaultTemplate"/> will be used.
    /// </value>
    public DataTemplate? GeometryTemplate { get; set; }

    /// <summary>
    /// Gets or sets the template used for camera components (type names "PerspectiveCamera" or
    /// "OrthographicCamera").
    /// </summary>
    /// <value>
    /// A <see cref="DataTemplate"/> specifically for camera components. May be <see langword="null"/>
    /// in which case <see cref="DefaultTemplate"/> will be used.
    /// </value>
    public DataTemplate? CameraTemplate { get; set; }

    /// <summary>
    /// Selects the <see cref="DataTemplate"/> to use for the provided item.
    /// </summary>
    /// <param name="item">The data object for which to select a template. May be <see langword="null"/>.</param>
    /// <param name="container">The container that will host the returned template. Not used by this implementation.</param>
    /// <returns>
    /// The selected <see cref="DataTemplate"/>, or <see langword="null"/> when no template is available.
    /// </returns>
    protected override DataTemplate? SelectTemplateCore(object item, DependencyObject container)
    {
        _ = container;

        if (item is null)
        {
            return this.DefaultTemplate;
        }

        return item.GetType().Name switch
        {
            "TransformComponent" => this.TransformTemplate ?? this.DefaultTemplate,
            "MeshComponent" or "GeometryComponent" => this.GeometryTemplate ?? this.DefaultTemplate,
            "PerspectiveCamera" or "OrthographicCamera" => this.CameraTemplate ?? this.DefaultTemplate,
            _ => this.DefaultTemplate,
        };
    }
}
