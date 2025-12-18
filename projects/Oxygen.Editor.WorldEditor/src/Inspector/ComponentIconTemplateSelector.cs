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
public sealed class ComponentIconTemplateSelector : DataTemplateSelector
{
    public DataTemplate? DefaultTemplate { get; set; }

    public DataTemplate? TransformTemplate { get; set; }

    public DataTemplate? GeometryTemplate { get; set; }

    public DataTemplate? CameraTemplate { get; set; }

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
