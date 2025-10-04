// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using Microsoft.UI.Xaml;
using Windows.Foundation;

namespace DroidNet.Controls;

/// <summary>
///     Event arguments describing a submenu request routed through <see cref="MenuInteractionController"/>.
/// </summary>
public sealed class MenuSubmenuRequestEventArgs : EventArgs
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="MenuSubmenuRequestEventArgs"/> class.
    /// </summary>
    /// <param name="menuItem">The menu item requesting a submenu.</param>
    /// <param name="origin">The UI element that triggered the request.</param>
    /// <param name="bounds">The bounds of the origin element in root coordinates.</param>
    /// <param name="columnLevel">The zero-based column level for the origin item.</param>
    /// <param name="navigationMode">The navigation mode that triggered the request.</param>
    public MenuSubmenuRequestEventArgs(MenuItemData menuItem, FrameworkElement origin, Rect bounds, int columnLevel, MenuNavigationMode navigationMode)
    {
        this.MenuItem = menuItem ?? throw new ArgumentNullException(nameof(menuItem));
        this.Origin = origin ?? throw new ArgumentNullException(nameof(origin));
        this.Bounds = bounds;
        this.ColumnLevel = columnLevel;
        this.NavigationMode = navigationMode;
    }

    /// <summary>
    ///     Gets the menu item requesting a submenu.
    /// </summary>
    public MenuItemData MenuItem { get; }

    /// <summary>
    ///     Gets the element that triggered the request.
    /// </summary>
    public FrameworkElement Origin { get; }

    /// <summary>
    ///     Gets the origin bounds in the visual root coordinate space.
    /// </summary>
    public Rect Bounds { get; }

    /// <summary>
    ///     Gets the zero-based column level for the origin item (0 == root column).
    /// </summary>
    public int ColumnLevel { get; }

    /// <summary>
    ///     Gets the navigation mode that triggered the request.
    /// </summary>
    public MenuNavigationMode NavigationMode { get; }
}
