// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls;

/// <summary>
/// Static helper members for <see cref="MenuBar"/>.
/// </summary>
public sealed partial class MenuBar
{
    private static void OnMenuSourceChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        var control = (MenuBar)d;
        if (control.rootItemsRepeater is ItemsRepeater repeater)
        {
            repeater.ItemsSource = ((IMenuSource?)e.NewValue)?.Items;
        }

        var newSource = (IMenuSource?)e.NewValue;
        control.AttachController(newSource?.Services.InteractionController);

        control.CloseActiveFlyout();
        control.OpenRootIndex = -1;
        control.IsSubmenuOpen = false;
    }
}
