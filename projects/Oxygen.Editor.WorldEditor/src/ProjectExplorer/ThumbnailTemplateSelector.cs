// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Microsoft.UI.Xaml.Controls;
using DataTemplate = Microsoft.UI.Xaml.DataTemplate;
using DependencyObject = Microsoft.UI.Xaml.DependencyObject;

namespace Oxygen.Editor.WorldEditor.ProjectExplorer;

/// <summary>
/// A <see cref="DataTemplateSelector"/> that maps a <see cref="TreeItemAdapter"/> to a template that can be used to display a
/// <see cref="Thumbnail"/> for it.
/// </summary>
public partial class ThumbnailTemplateSelector : DataTemplateSelector
{
    /// <summary>
    /// Gets or sets the template used for scenes.
    /// </summary>
    public DataTemplate? SceneTemplate { get; set; }

    /// <summary>
    /// Gets or sets the template used for entities.
    /// </summary>
    public DataTemplate? EntityTemplate { get; set; }

    /// <summary>
    /// Selects a data template based on the type of the item.
    /// </summary>
    /// <param name="item">The item for which to select the template.</param>
    /// <param name="container">The container in which the item is displayed.</param>
    /// <returns>A <see cref="DataTemplate"/> that matches the type of the item, or <see langword="null"/> if no matching template is found.</returns>
    protected override DataTemplate? SelectTemplateCore(object item, DependencyObject container)
        => item switch
        {
            SceneAdapter => this.SceneTemplate,
            SceneNodeAdapter => this.EntityTemplate,
            _ => null,
        };
}
