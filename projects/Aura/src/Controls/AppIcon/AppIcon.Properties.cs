// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Aura.Controls;

internal partial class AppIcon
{
    public static readonly DependencyProperty IconSourceProperty =
        DependencyProperty.Register(
            nameof(IconSource),
            typeof(IconSource),
            typeof(AppIcon),
            new PropertyMetadata(defaultValue: null));

    public IconSource IconSource
    {
        get => (IconSource)this.GetValue(IconSourceProperty);
        set => this.SetValue(IconSourceProperty, value);
    }
}
