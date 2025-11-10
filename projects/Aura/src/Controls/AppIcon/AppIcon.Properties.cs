// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Aura.Controls;

/// <summary>
///     Represents an application icon control for displaying app icons in the UI.
/// </summary>
internal partial class AppIcon
{
    /// <summary>
    ///     Identifies the <see cref="IconSource"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty IconSourceProperty =
        DependencyProperty.Register(
            nameof(IconSource),
            typeof(IconSource),
            typeof(AppIcon),
            new PropertyMetadata(defaultValue: null));

    /// <summary>
    ///     Gets or sets the source of the icon to display.
    /// </summary>
    public IconSource IconSource
    {
        get => (IconSource)this.GetValue(IconSourceProperty);
        set => this.SetValue(IconSourceProperty, value);
    }
}
