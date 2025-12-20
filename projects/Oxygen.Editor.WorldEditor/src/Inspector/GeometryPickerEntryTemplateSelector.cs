// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;
using DataTemplate = Microsoft.UI.Xaml.DataTemplate;
using DependencyObject = Microsoft.UI.Xaml.DependencyObject;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// Template selector used by the geometry asset picker list to choose between group header
/// and item templates.
/// </summary>
/// <remarks>
/// The selector examines the runtime type of the data item and returns <see cref="HeaderTemplate"/>
/// for <see cref="GeometryAssetGroupHeader"/> instances and <see cref="ItemTemplate"/> for
/// <see cref="GeometryAssetPickerItem"/> instances. For unknown types the selector falls back
/// to <see cref="ItemTemplate"/>.
/// </remarks>
public sealed class GeometryPickerEntryTemplateSelector : DataTemplateSelector
{
    /// <summary>
    /// Gets or sets the template used to render group headers in the picker list.
    /// May be <see langword="null"/>.
    /// </summary>
    public DataTemplate? HeaderTemplate { get; set; }

    /// <summary>
    /// Gets or sets the template used to render individual asset items in the picker list.
    /// May be <see langword="null"/>.
    /// </summary>
    public DataTemplate? ItemTemplate { get; set; }

    /// <summary>
    /// Selects the appropriate <see cref="DataTemplate"/> for the given item.
    /// </summary>
    /// <param name="item">The data item for which to select a template. May be <see langword="null"/>.</param>
    /// <param name="container">The container that will host the returned template. This implementation does not use the container parameter.</param>
    /// <returns>
    /// The selected <see cref="DataTemplate"/>, or <see langword="null"/> when no suitable template is set.
    /// </returns>
    protected override DataTemplate? SelectTemplateCore(object item, DependencyObject container)
    {
        _ = container;

        return item switch
        {
            GeometryAssetGroupHeader => this.HeaderTemplate,
            GeometryAssetPickerItem => this.ItemTemplate,
            _ => this.ItemTemplate,
        };
    }
}
