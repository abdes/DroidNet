// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using Microsoft.UI.Xaml;
using Windows.UI.Xaml.Controls;

/// <summary>
/// Properties for the custom thumbnail control.
/// </summary>
public partial class Thumbnail
{
    /// <summary>
    /// The backing <see cref="DependencyProperty" /> for the <see cref="CustomTemplate" /> property.
    /// </summary>
    private static readonly DependencyProperty CustomTemplateProperty =
        DependencyProperty.Register(
            nameof(CustomTemplate),
            typeof(DataTemplate),
            typeof(Thumbnail),
            new PropertyMetadata(defaultValue: default));

    /// <summary>
    /// Gets the custom data template for the thumbnail control.
    /// </summary>
    /// <value>
    /// A <see cref="DataTemplate" /> that defines the appearance of the thumbnail content.
    /// </value>
    /// <remarks>
    /// When the control has a <see cref="ContentControl.ContentTemplateSelector" />, the value of this property is automatically
    /// set to the template provided by that template selector, and the control's visual state is switched to the custom template
    /// state. There is no valid use case for manually setting it.
    /// </remarks>
    public DataTemplate? CustomTemplate
    {
        get => (DataTemplate)this.GetValue(CustomTemplateProperty);
        private set => this.SetValue(CustomTemplateProperty, value);
    }
}
