// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

/// <summary>
/// Properties for the <see cref="DynamicTree" /> control.
/// </summary>
public partial class DynamicTree
{
    /// <summary>
    /// The backing <see cref="DependencyProperty" /> for the <see cref="SelectionMode" /> property.
    /// </summary>
    public static readonly DependencyProperty SelectionModeProperty = DependencyProperty.Register(
        nameof(SelectionMode),
        typeof(SelectionMode),
        typeof(DynamicTree),
        new PropertyMetadata(SelectionMode.None));

    /// <summary>
    /// The backing <see cref="DependencyProperty" /> for the <see cref="ThumbnailTemplateSelector" /> property.
    /// </summary>
    public static readonly DependencyProperty ThumbnailTemplateSelectorProperty = DependencyProperty.Register(
        nameof(ThumbnailTemplateSelector),
        typeof(DataTemplateSelector),
        typeof(DynamicTree),
        new PropertyMetadata(default(DataTemplateSelector)));

    /// <summary>
    /// Gets or sets the selection mode of the dynamic tree.
    /// </summary>
    public SelectionMode SelectionMode
    {
        get => (SelectionMode)this.GetValue(SelectionModeProperty);
        set => this.SetValue(SelectionModeProperty, value);
    }

    /// <summary>
    /// Gets or sets the data template selector for the thumbnails in the dynamic tree.
    /// </summary>
    public DataTemplateSelector ThumbnailTemplateSelector
    {
        get => (DataTemplateSelector)this.GetValue(ThumbnailTemplateSelectorProperty);
        set => this.SetValue(ThumbnailTemplateSelectorProperty, value);
    }
}
