// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.Linq;
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
    private readonly Dictionary<int, MenuItemData> activeByColumn = new();

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
    ///     Occurs when the current menu chain should be dismissed by host surfaces.
    /// </summary>
    public event EventHandler? DismissRequested;

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
        var columnLevel = this.FindColumnLevel(menuItem);
        if (columnLevel < 0)
        {
            columnLevel = menuItem.HasChildren ? 0 : this.activeByColumn.Keys.DefaultIfEmpty(-1).Max();
        }

        this.ActivateItem(menuItem, Math.Max(columnLevel, 0), this.NavigationMode);
        this.services.HandleGroupSelection(menuItem);

        this.ItemInvoked?.Invoke(this, new MenuItemInvokedEventArgs { MenuItem = menuItem });
        this.Dismiss();
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

        this.ActivateItem(menuItem, columnLevel, inputSource);

        if (!menuItem.HasChildren)
        {
            return;
        }

        var bounds = new Rect(0, 0, origin.ActualWidth, origin.ActualHeight);

        this.SubmenuRequested?.Invoke(
            this,
            new MenuSubmenuRequestEventArgs(menuItem, origin, bounds, columnLevel, inputSource));
    }

    /// <summary>
    ///     Activates a menu item and deactivates any descendants beyond the specified column level.
    /// </summary>
    /// <param name="menuItem">The menu item to activate.</param>
    /// <param name="columnLevel">The zero-based column level for the item (0 == root).</param>
    /// <param name="inputSource">The navigation mode that triggered the change.</param>
    public void ActivateItem(MenuItemData menuItem, int columnLevel, MenuNavigationMode inputSource)
    {
        ArgumentNullException.ThrowIfNull(menuItem);

        this.NavigationMode = inputSource;

        foreach (var key in this.activeByColumn.Keys.Where(k => k >= columnLevel).ToList())
        {
            if (!ReferenceEquals(this.activeByColumn[key], menuItem))
            {
                this.activeByColumn[key].IsActive = false;
            }

            this.activeByColumn.Remove(key);
        }

        menuItem.IsActive = true;
        this.activeByColumn[columnLevel] = menuItem;
    }

    /// <summary>
    ///     Clears any active menu path and signals dismissal to host surfaces.
    /// </summary>
    public void Dismiss()
    {
        foreach (var menuItem in this.activeByColumn.Values.Distinct())
        {
            menuItem.IsActive = false;
        }

        this.activeByColumn.Clear();
        this.DismissRequested?.Invoke(this, EventArgs.Empty);
    }

    private int FindColumnLevel(MenuItemData menuItem)
    {
        foreach (var entry in this.activeByColumn)
        {
            if (ReferenceEquals(entry.Value, menuItem))
            {
                return entry.Key;
            }
        }

        return -1;
    }
}
