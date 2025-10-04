// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Linq;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;

namespace DroidNet.Controls;

/// <summary>
///     Horizontal menu bar control that renders root <see cref="MenuItemData"/> instances and
///     materializes cascading submenus through the custom <see cref="MenuFlyout"/> presenter.
/// </summary>
[TemplatePart(Name = RootItemsRepeaterPart, Type = typeof(ItemsRepeater))]
public sealed class MenuBar : Control
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
    ///     Identifies the <see cref="IsSubmenuOpen"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty IsSubmenuOpenProperty = DependencyProperty.Register(
        nameof(IsSubmenuOpen),
        typeof(bool),
        typeof(MenuBar),
        new PropertyMetadata(false));

    /// <summary>
    ///     Identifies the <see cref="OpenRootIndex"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty OpenRootIndexProperty = DependencyProperty.Register(
        nameof(OpenRootIndex),
        typeof(int),
        typeof(MenuBar),
        new PropertyMetadata(-1));

    private const string RootItemsRepeaterPart = "PART_RootItemsRepeater";

    private ItemsRepeater? rootItemsRepeater;
    private MenuFlyout? activeFlyout;
    private MenuItem? activeRootItem;
    private MenuItem? pendingRootItem;
    private MenuInteractionController? controller;

    /// <summary>
    ///     Initializes a new instance of the <see cref="MenuBar"/> class.
    /// </summary>
    public MenuBar()
    {
        this.DefaultStyleKey = typeof(MenuBar);
    }

    /// <summary>
    ///     Occurs when a leaf menu item is invoked through the bar.
    /// </summary>
    public event EventHandler<MenuItemInvokedEventArgs>? ItemInvoked;

    /// <summary>
    ///     Gets or sets the menu source that provides items and shared services for the bar.
    /// </summary>
    public IMenuSource? MenuSource
    {
        get => (IMenuSource?)this.GetValue(MenuSourceProperty);
        set => this.SetValue(MenuSourceProperty, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether a submenu is currently visible.
    /// </summary>
    public bool IsSubmenuOpen
    {
        get => (bool)this.GetValue(IsSubmenuOpenProperty);
        set => this.SetValue(IsSubmenuOpenProperty, value);
    }

    /// <summary>
    ///     Gets or sets the zero-based index of the root item whose submenu is open.
    /// </summary>
    public int OpenRootIndex
    {
        get => (int)this.GetValue(OpenRootIndexProperty);
        set => this.SetValue(OpenRootIndexProperty, value);
    }

    /// <inheritdoc />
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        if (this.rootItemsRepeater != null)
        {
            this.rootItemsRepeater.ElementPrepared -= this.OnRootItemPrepared;
            this.rootItemsRepeater.ElementClearing -= this.OnRootItemClearing;
        }

        this.rootItemsRepeater = this.GetTemplateChild(RootItemsRepeaterPart) as ItemsRepeater
            ?? throw new InvalidOperationException($"{nameof(MenuBar)} template must declare an ItemsRepeater named '{RootItemsRepeaterPart}'.");

        this.rootItemsRepeater.ItemsSource = this.MenuSource?.Items;
        this.rootItemsRepeater.ElementPrepared += this.OnRootItemPrepared;
        this.rootItemsRepeater.ElementClearing += this.OnRootItemClearing;

        this.AttachController(this.MenuSource?.Services.InteractionController);
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
        control.IsSubmenuOpen = false;
    }

    private void OnRootItemPrepared(ItemsRepeater sender, ItemsRepeaterElementPreparedEventArgs args)
    {
        if (args.Element is not MenuItem menuItem || menuItem.ItemData is null)
        {
            return;
        }

        menuItem.Invoked += this.OnRootMenuItemInvoked;
        menuItem.SubmenuRequested += this.OnRootMenuItemSubmenuRequested;
        menuItem.HoverEntered += this.OnRootMenuItemHoverEntered;
        menuItem.RadioGroupSelectionRequested += this.OnRootRadioGroupSelectionRequested;
        menuItem.PointerEntered += this.OnRootMenuItemPointerEntered;
        menuItem.ShowSubmenuGlyph = false;
    }

    private void OnRootItemClearing(ItemsRepeater sender, ItemsRepeaterElementClearingEventArgs args)
    {
        if (args.Element is not MenuItem menuItem)
        {
            return;
        }

        menuItem.Invoked -= this.OnRootMenuItemInvoked;
        menuItem.SubmenuRequested -= this.OnRootMenuItemSubmenuRequested;
        menuItem.HoverEntered -= this.OnRootMenuItemHoverEntered;
        menuItem.RadioGroupSelectionRequested -= this.OnRootRadioGroupSelectionRequested;
        menuItem.PointerEntered -= this.OnRootMenuItemPointerEntered;
        menuItem.ShowSubmenuGlyph = true;

        if (ReferenceEquals(menuItem, this.activeRootItem))
        {
            this.activeRootItem = null;
        }
    }

    private void OnRootRadioGroupSelectionRequested(object? sender, MenuItemRadioGroupEventArgs e)
    {
        if (this.controller is not null)
        {
            this.controller.HandleRadioGroupSelection(e.MenuItem);
        }
        else
        {
            this.MenuSource?.Services.HandleGroupSelection(e.MenuItem);
        }
    }

    private void OnRootMenuItemHoverEntered(object? sender, MenuItemHoverEventArgs e)
    {
        if (sender is not MenuItem menuItem)
        {
            return;
        }

        this.HandleRootPointerActivation(menuItem, e.MenuItem);
    }

    private void OnRootMenuItemPointerEntered(object sender, PointerRoutedEventArgs e)
    {
        if (sender is not MenuItem menuItem || menuItem.ItemData is null)
        {
            return;
        }

        this.HandleRootPointerActivation(menuItem, menuItem.ItemData);
    }

    private void HandleRootPointerActivation(MenuItem menuItem, MenuItemData menuItemData)
    {
        if (this.controller is null || !menuItemData.HasChildren)
        {
            return;
        }

        this.controller.NotifyPointerNavigation();

        if (!this.IsSubmenuOpen)
        {
            return;
        }

        if (ReferenceEquals(menuItem, this.activeRootItem) || ReferenceEquals(menuItem, this.pendingRootItem))
        {
            return;
        }

        this.pendingRootItem = menuItem;
        this.controller.RequestSubmenu(menuItem, menuItemData, 0, MenuNavigationMode.PointerInput);
    }

    private void OnRootMenuItemSubmenuRequested(object? sender, MenuItemSubmenuEventArgs e)
    {
        if (this.controller is null || sender is not MenuItem menuItem)
        {
            return;
        }

        this.controller.NotifyKeyboardNavigation();
        if (ReferenceEquals(menuItem, this.activeRootItem) || ReferenceEquals(menuItem, this.pendingRootItem))
        {
            return;
        }

        this.pendingRootItem = menuItem;
        this.controller.RequestSubmenu(menuItem, e.MenuItem, 0, MenuNavigationMode.KeyboardInput);
    }

    private void OnRootMenuItemInvoked(object? sender, MenuItemInvokedEventArgs e)
    {
        if (this.controller is null || sender is not MenuItem menuItem)
        {
            return;
        }

        if (e.MenuItem.HasChildren)
        {
            var mode = this.controller.NavigationMode == MenuNavigationMode.KeyboardInput
                ? MenuNavigationMode.KeyboardInput
                : MenuNavigationMode.PointerInput;

            if (mode == MenuNavigationMode.PointerInput)
            {
                this.controller.NotifyPointerNavigation();
            }

            if (ReferenceEquals(menuItem, this.activeRootItem) || ReferenceEquals(menuItem, this.pendingRootItem))
            {
                return;
            }

            this.pendingRootItem = menuItem;
            this.controller.RequestSubmenu(menuItem, e.MenuItem, 0, mode);
            return;
        }

        this.controller.HandleItemInvoked(e.MenuItem);
    }

    private void OpenRootSubmenu(MenuSubmenuRequestEventArgs request)
    {
        if (this.MenuSource is null)
        {
            this.pendingRootItem = null;
            return;
        }

        if (!request.MenuItem.SubItems.Any())
        {
            this.pendingRootItem = null;
            return;
        }

        if (this.activeFlyout is not null)
        {
            this.activeFlyout.SuppressControllerDismissal = true;
            this.activeFlyout.Closed -= this.OnFlyoutClosed;
            this.activeFlyout.OverlayInputPassThroughElement = null;
            this.activeFlyout.Hide();
            this.activeFlyout = null;
        }

        var submenuSource = new MenuSourceView(request.MenuItem.SubItems, this.MenuSource.Services);
        var flyout = new MenuFlyout
        {
            MenuSource = submenuSource,
            Placement = FlyoutPlacementMode.BottomEdgeAlignedLeft,
            OwnerNavigationMode = request.NavigationMode,
            Controller = this.controller,
        };

        flyout.OverlayInputPassThroughElement = this;
        flyout.Closed += this.OnFlyoutClosed;

        var index = this.MenuSource.Items.IndexOf(request.MenuItem);
        this.OpenRootIndex = index >= 0 ? index : -1;
        this.IsSubmenuOpen = true;
        this.activeRootItem = request.Origin as MenuItem;
        this.pendingRootItem = null;
        this.activeFlyout = flyout;

        var options = new FlyoutShowOptions
        {
            Placement = FlyoutPlacementMode.BottomEdgeAlignedLeft,
        };

        flyout.ShowAt(request.Origin, options);
    }

    private void OnControllerSubmenuRequested(object? sender, MenuSubmenuRequestEventArgs e)
    {
        if (e.ColumnLevel == 0)
        {
            this.OpenRootSubmenu(e);
        }
    }

    private void OnControllerItemInvoked(object? sender, MenuItemInvokedEventArgs e)
    {
        this.ItemInvoked?.Invoke(this, e);
    }

    private void OnControllerDismissRequested(object? sender, EventArgs e)
    {
        this.CloseActiveFlyout();
    }

    private void OnFlyoutClosed(object? sender, object e)
    {
        this.CloseActiveFlyout();
    }

    private void CloseActiveFlyout()
    {
        if (this.activeFlyout is not null)
        {
            this.activeFlyout.SuppressControllerDismissal = true;
            this.activeFlyout.Closed -= this.OnFlyoutClosed;
            this.activeFlyout.OverlayInputPassThroughElement = null;
            this.activeFlyout.Hide();
            this.activeFlyout = null;
        }

        this.IsSubmenuOpen = false;
        this.OpenRootIndex = -1;
        this.activeRootItem = null;
        this.pendingRootItem = null;
    }

    private void AttachController(MenuInteractionController? newController)
    {
        if (ReferenceEquals(this.controller, newController))
        {
            return;
        }

        if (this.controller is MenuInteractionController oldController)
        {
            oldController.SubmenuRequested -= this.OnControllerSubmenuRequested;
            oldController.ItemInvoked -= this.OnControllerItemInvoked;
            oldController.DismissRequested -= this.OnControllerDismissRequested;
        }

        this.pendingRootItem = null;
        this.controller = newController;

        if (newController is not null)
        {
            newController.SubmenuRequested += this.OnControllerSubmenuRequested;
            newController.ItemInvoked += this.OnControllerItemInvoked;
            newController.DismissRequested += this.OnControllerDismissRequested;
        }
    }
}
