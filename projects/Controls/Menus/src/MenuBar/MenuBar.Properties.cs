// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls;

/// <summary>
///     Horizontal menu bar control that renders root <see cref="MenuItemData"/> instances and
///     materializes cascading submenus through the custom <see cref="MenuFlyout"/> presenter.
/// </summary>
public sealed partial class MenuBar
{
    /// <summary>
    ///     Identifies the <see cref="MenuSource"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty MenuSourceProperty = DependencyProperty.Register(
        nameof(MenuSource),
        typeof(IMenuSource),
        typeof(MenuBar),
        new PropertyMetadata(null, OnMenuSourceChanged));

    /// <summary>
    ///     Gets or sets the menu source that provides items and shared services for the bar.
    /// </summary>
    public IMenuSource? MenuSource
    {
        get => (IMenuSource?)this.GetValue(MenuSourceProperty);
        set => this.SetValue(MenuSourceProperty, value);
    }

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
    }
}
