// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Microsoft.UI.Xaml.Controls;
using DataTemplate = Microsoft.UI.Xaml.DataTemplate;
using DependencyObject = Microsoft.UI.Xaml.DependencyObject;

namespace Oxygen.Editor.WorldEditor.ProjectExplorer;

/// <summary>
/// A <see cref="DataTemplateSelector" /> that can map a <see cref="TreeItemAdapter" /> to a template that can be used to display a
/// <see cref="Thumbnail" /> for it.
/// </summary>
public partial class ThumbnailTemplateSelector : DataTemplateSelector
{
    public DataTemplate? SceneTemplate { get; set; }

    public DataTemplate? EntityTemplate { get; set; }

    /// <inheritdoc/>
    protected override DataTemplate? SelectTemplateCore(object item, DependencyObject container)
        => item switch
        {
            SceneAdapter => this.SceneTemplate,
            GameEntityAdapter => this.EntityTemplate,
            _ => null,
        };
}
