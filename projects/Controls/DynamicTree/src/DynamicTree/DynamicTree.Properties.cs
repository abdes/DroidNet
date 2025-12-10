// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls;

/// <summary>
///     Properties for the <see cref="DynamicTree" /> control.
/// </summary>
public partial class DynamicTree
{
    /// <summary>
    ///     The backing <see cref="DependencyProperty" /> for the <see cref="ThumbnailTemplateSelector" /> property.
    /// </summary>
    public static readonly DependencyProperty ThumbnailTemplateSelectorProperty = DependencyProperty.Register(
        nameof(ThumbnailTemplateSelector),
        typeof(DataTemplateSelector),
        typeof(DynamicTree),
        new PropertyMetadata(defaultValue: null));

    /// <summary>
    ///     Gets or sets the data template selector for the thumbnails in the dynamic tree.
    /// </summary>
    public DataTemplateSelector ThumbnailTemplateSelector
    {
        get => (DataTemplateSelector)this.GetValue(ThumbnailTemplateSelectorProperty);
        set => this.SetValue(ThumbnailTemplateSelectorProperty, value);
    }
}
