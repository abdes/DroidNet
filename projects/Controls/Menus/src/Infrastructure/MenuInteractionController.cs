// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using Microsoft.UI.Xaml;
using Windows.Foundation;

namespace DroidNet.Controls;

/// <summary>
///     Coordinates interaction flows shared between menu containers. The controller keeps
///     track of the current navigation modality (pointer vs. keyboard), applies selection
///     logic through <see cref="MenuServices"/>, and raises higher-level events so menu
///     surfaces can materialize columns or dismiss open presenters consistently.
/// </summary>
public sealed class MenuInteractionController
{
    private readonly MenuServices services;

    /// <summary>
    ///     Initializes a new instance of the <see cref="MenuInteractionController"/> class.
    /// </summary>
    /// <param name="services">The shared services instance produced by <see cref="MenuBuilder"/>.</param>
    public MenuInteractionController(MenuServices services)
    {
        this.services = services ?? throw new ArgumentNullException(nameof(services));
    }

    /// <summary>
    ///     Occurs when a submenu needs to be displayed for a given menu item.
    /// </summary>
    public event EventHandler<MenuSubmenuRequestEventArgs>? SubmenuRequested;

    /// <summary>
    ///     Occurs when a menu item has been invoked and containers should close or propagate
    ///     the execution event further up the chain.
    /// </summary>
    public event EventHandler<MenuItemInvokedEventArgs>? ItemInvoked;

    /// <summary>
    ///     Gets the current navigation mode used to open the active menu path.
    /// </summary>
    public MenuNavigationMode NavigationMode { get; private set; } = MenuNavigationMode.PointerInput;

    /// <summary>
    ///     Signals that the most recent interaction originated from pointer input.
    /// </summary>
    public void NotifyPointerNavigation() => this.NavigationMode = MenuNavigationMode.PointerInput;

    /// <summary>
    ///     Signals that the most recent interaction originated from keyboard input.
    /// </summary>
    public void NotifyKeyboardNavigation() => this.NavigationMode = MenuNavigationMode.KeyboardInput;

    /// <summary>
    ///     Handles a radio-group selection request by delegating to <see cref="MenuServices"/>.
    /// </summary>
    /// <param name="menuItem">The menu item requesting radio-group coordination.</param>
    public void HandleRadioGroupSelection(MenuItemData menuItem)
    {
        ArgumentNullException.ThrowIfNull(menuItem);
        this.services.HandleGroupSelection(menuItem);
    }

    /// <summary>
    ///     Handles an invoked menu item by raising <see cref="ItemInvoked"/> so containers
    ///     can dismiss the current menu chain while keeping selection logic consistent.
    /// </summary>
    /// <param name="menuItem">The menu item that was invoked.</param>
    public void HandleItemInvoked(MenuItemData menuItem)
    {
        ArgumentNullException.ThrowIfNull(menuItem);
        this.ItemInvoked?.Invoke(this, new MenuItemInvokedEventArgs { MenuItem = menuItem });
    }

    /// <summary>
    ///     Requests a submenu for a specific menu item. The controller resolves the anchor
    ///     bounds in root coordinates so the host can position popups accurately.
    /// </summary>
    /// <param name="origin">The menu item control that originated the request.</param>
    /// <param name="menuItem">The data for which a submenu should be shown.</param>
    /// <param name="columnLevel">The zero-based column level (0 == root column).</param>
    /// <param name="inputSource">Indicates whether pointer or keyboard triggered the request.</param>
    public void RequestSubmenu(FrameworkElement origin, MenuItemData menuItem, int columnLevel, MenuNavigationMode inputSource)
    {
        ArgumentNullException.ThrowIfNull(origin);
        ArgumentNullException.ThrowIfNull(menuItem);

        this.NavigationMode = inputSource;

        var bounds = new Rect(0, 0, origin.ActualWidth, origin.ActualHeight);

        this.SubmenuRequested?.Invoke(
            this,
            new MenuSubmenuRequestEventArgs(menuItem, origin, bounds, columnLevel, inputSource));
    }
}
