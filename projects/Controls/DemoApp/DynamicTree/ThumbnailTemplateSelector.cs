// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Demo.DynamicTree;

/// <summary>
/// A <see cref="DataTemplateSelector" /> that can map a <see cref="TreeItemAdapter" /> to a template that can be used to display a
/// <see cref="Thumbnail" /> for it.
/// </summary>
public partial class ThumbnailTemplateSelector : DataTemplateSelector
{
    /// <inheritdoc/>
    protected override DataTemplate SelectTemplateCore(object item, DependencyObject container) => item is SceneAdapter
            ? (DataTemplate)Application.Current.Resources["SceneThumbnailTemplate"]
            : item is EntityAdapter
            ? (DataTemplate)Application.Current.Resources["EntityThumbnailTemplate"]
            : (DataTemplate)Application.Current.Resources["DefaultThumbnailTemplate"];
}
