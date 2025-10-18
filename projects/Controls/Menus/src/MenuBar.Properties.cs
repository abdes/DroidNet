// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Horizontal menu bar control that renders root <see cref="MenuItemData"/> instances and
///     materializes cascading submenus through an <see cref="ICascadedMenuHost"/> implementation.
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
        new PropertyMetadata(defaultValue: null, OnMenuSourceChanged));

    /// <summary>
    ///     Identifies the <see cref="DismissOnFlyoutDismissal"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty DismissOnFlyoutDismissalProperty = DependencyProperty.Register(
        nameof(DismissOnFlyoutDismissal),
        typeof(bool),
        typeof(MenuBar),
        new PropertyMetadata(defaultValue: false));

    /// <summary>
    ///     Gets or sets the menu source that provides items and shared services for the bar.
    /// </summary>
    public IMenuSource? MenuSource
    {
        get => (IMenuSource?)this.GetValue(MenuSourceProperty);
        set => this.SetValue(MenuSourceProperty, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether <see cref="Dismissed"/> should be raised when the cascaded host closes.
    /// </summary>
    public bool DismissOnFlyoutDismissal
    {
        get => (bool)this.GetValue(DismissOnFlyoutDismissalProperty);
        set => this.SetValue(DismissOnFlyoutDismissalProperty, value);
    }

    private static void OnMenuSourceChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        var control = (MenuBar)d;
        var menuSource = (IMenuSource?)e.NewValue;

        control.SetRootItemsCollection(menuSource?.Items);
        control.RebuildRootItems();

        if (control.activeHost is { IsOpen: true } host)
        {
            host.Dismiss(MenuDismissKind.Programmatic);
        }
    }
}
