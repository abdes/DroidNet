// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Windows.System;

namespace DroidNet.Controls;

/// <summary>
///     Horizontal menu bar control that renders root <see cref="MenuItemData"/> instances and
///     materializes cascading submenus through the custom <see cref="MenuFlyout"/> presenter.
/// </summary>
[TemplatePart(Name = RootItemsRepeaterPart, Type = typeof(ItemsRepeater))]
public sealed partial class MenuBar : Control, IMenuInteractionSurface
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

        if (this.rootItemsRepeater is not null)
        {
            this.rootItemsRepeater.ElementPrepared -= this.OnRootItemPrepared;
            this.rootItemsRepeater.ElementClearing -= this.OnRootItemClearing;
        }

        this.rootItemsRepeater = this.GetTemplateChild(RootItemsRepeaterPart) as ItemsRepeater
            ?? throw new InvalidOperationException($"{nameof(MenuBar)} template must declare an ItemsRepeater named '{RootItemsRepeaterPart}'.");

        this.rootItemsRepeater.ItemsSource = this.MenuSource?.Items;
        this.rootItemsRepeater.ElementPrepared += this.OnRootItemPrepared;
        this.rootItemsRepeater.ElementClearing += this.OnRootItemClearing;

        // Ensure we are listening for keyboard events on the bar so we can forward navigation
        this.KeyDown -= this.HandleBarKeyDown;
        this.KeyDown += this.HandleBarKeyDown;

        this.AttachController(this.MenuSource?.Services.InteractionController);
    }

    private void OnRootItemPrepared(ItemsRepeater sender, ItemsRepeaterElementPreparedEventArgs args)
    {
        if (args.Element is not MenuItem menuItem || menuItem.ItemData is null)
        {
            return;
        }

        menuItem.Invoked += this.OnRootMenuItemInvoked;
        menuItem.SubmenuRequested += this.OnRootMenuItemSubmenuRequested;
        menuItem.HoverStarted += this.OnRootMenuItemHoverStarted;
        menuItem.RadioGroupSelectionRequested += this.OnRootRadioGroupSelectionRequested;
        menuItem.PointerEntered += this.OnRootMenuItemPointerEntered;
        menuItem.GotFocus += this.OnRootMenuItemGotFocus;
        menuItem.PreviewKeyDown += this.OnRootMenuItemPreviewKeyDown;
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
        menuItem.HoverStarted -= this.OnRootMenuItemHoverStarted;
        menuItem.RadioGroupSelectionRequested -= this.OnRootRadioGroupSelectionRequested;
        menuItem.PointerEntered -= this.OnRootMenuItemPointerEntered;
        menuItem.GotFocus -= this.OnRootMenuItemGotFocus;
        menuItem.PreviewKeyDown -= this.OnRootMenuItemPreviewKeyDown;
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
            this.controller.OnRadioGroupSelectionRequested(e.ItemData);
        }
        else
        {
            this.MenuSource?.Services.HandleGroupSelection(e.ItemData);
        }
    }

    private void OnRootMenuItemHoverStarted(object? sender, MenuItemHoverEventArgs e)
    {
        if (sender is not MenuItem menuItem)
        {
            return;
        }

        this.HandleRootPointerActivation(menuItem, e.ItemData);
    }

    private void OnRootMenuItemPointerEntered(object sender, PointerRoutedEventArgs e)
    {
        _ = e;

        if (sender is not MenuItem menuItem || menuItem.ItemData is null)
        {
            return;
        }

        this.HandleRootPointerActivation(menuItem, menuItem.ItemData);
    }

    /// <summary>
    ///     Handles key events when the menu bar has focus and routes keyboard navigation to the controller.
    /// </summary>
    /// <param name="e">Key event args.</param>
    private void HandleBarKeyDown(object? sender, KeyRoutedEventArgs e)
    {
        if (this.controller is null)
        {
            return;
        }

        // Notify controller that keyboard is the current navigation source.
        this.controller.OnNavigationSourceChanged(MenuInteractionActivationSource.KeyboardInput);

        switch (e.Key)
        {
            case VirtualKey.Left:
            case VirtualKey.Right:
                e.Handled = this.HandleRootHorizontalNavigation(e.Key);
                break;

            case VirtualKey.Down:
            case VirtualKey.Enter:
            case VirtualKey.Space:
                e.Handled = this.HandleRootActivationKey(e.Key);
                break;
        }

        // no-op: handled routing is sufficient
    }

    private bool HandleRootHorizontalNavigation(VirtualKey key)
    {
        if (this.controller is null || this.MenuSource is null || this.MenuSource.Items.Count == 0)
        {
            return false;
        }

        var count = this.MenuSource.Items.Count;
        var focusedRoot = this.GetFocusedRootMenuItem();
        var focusedData = focusedRoot?.ItemData;
        var current = focusedData is not null ? this.MenuSource.Items.IndexOf(focusedData) : this.OpenRootIndex;

        if (current < 0)
        {
            current = 0;
        }

        var next = key == VirtualKey.Left
            ? (current - 1 + count) % count
            : (current + 1) % count;

        var target = this.MenuSource.Items[next];
        var container = this.ResolveRootContainer(target);

        if (container is null)
        {
            return false;
        }

        // Request focus on the root; if a submenu is open we keep it open for the new root.
        this.controller.OnFocusRequested(this.CreateRootContext(), container, target, MenuInteractionActivationSource.KeyboardInput, openSubmenu: this.IsSubmenuOpen);
        return true;
    }

    private bool HandleRootActivationKey(VirtualKey key)
    {
        if (this.controller is null || this.MenuSource is null || this.MenuSource.Items.Count == 0)
        {
            return false;
        }

        var activeRoot = this.GetFocusedRootMenuItem() ?? this.activeRootItem;

        if (activeRoot is null)
        {
            var fallbackIndex = this.OpenRootIndex >= 0 ? this.OpenRootIndex : 0;

            if (fallbackIndex < 0 || fallbackIndex >= this.MenuSource.Items.Count)
            {
                fallbackIndex = 0;
            }

            var fallbackData = this.MenuSource.Items[fallbackIndex];
            activeRoot = this.ResolveRootContainer(fallbackData);
        }

        if (activeRoot?.ItemData is not MenuItemData activeData)
        {
            return false;
        }

        if (activeData.HasChildren)
        {
            this.controller.OnFocusRequested(
                this.CreateRootContext(),
                activeRoot,
                activeData,
                MenuInteractionActivationSource.KeyboardInput,
                openSubmenu: true);
            return true;
        }

        if (key is VirtualKey.Enter or VirtualKey.Space)
        {
            this.controller.OnInvokeRequested(this.CreateRootContext(), activeData, MenuInteractionActivationSource.KeyboardInput);
            return true;
        }

        return false;
    }

    private void OnRootMenuItemGotFocus(object sender, RoutedEventArgs e)
    {
        if (this.controller is null || sender is not MenuItem menuItem || menuItem.ItemData is null)
        {
            return;
        }

        if (menuItem.FocusState != FocusState.Keyboard)
        {
            return;
        }

        var openSubmenu = this.IsSubmenuOpen && menuItem.ItemData.HasChildren;
        this.controller.OnFocusRequested(this.CreateRootContext(), menuItem, menuItem.ItemData, MenuInteractionActivationSource.KeyboardInput, openSubmenu);
    }

    private void OnRootMenuItemPreviewKeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (this.controller is null || sender is not MenuItem menuItem || menuItem.ItemData is null)
        {
            return;
        }

        this.controller.OnNavigationSourceChanged(MenuInteractionActivationSource.KeyboardInput);

        var handled = false;

        switch (e.Key)
        {
            case VirtualKey.Left:
                handled = this.FocusAdjacentRoot(menuItem.ItemData, -1);
                break;

            case VirtualKey.Right:
                handled = this.FocusAdjacentRoot(menuItem.ItemData, 1);
                break;

            case VirtualKey.Down:
            case VirtualKey.Enter:
            case VirtualKey.Space:
                handled = this.HandleRootActivationKey(e.Key);
                break;
        }

        if (handled)
        {
            e.Handled = true;
        }
    }

    private MenuItem? GetFocusedRootMenuItem()
    {
        if (this.MenuSource is null || this.MenuSource.Items.Count == 0)
        {
            return null;
        }

        if (FocusManager.GetFocusedElement(this.XamlRoot) is MenuItem focused && focused.ItemData is MenuItemData data)
        {
            var index = this.MenuSource.Items.IndexOf(data);
            if (index >= 0)
            {
                return focused;
            }
        }

        return null;
    }

    private bool FocusAdjacentRoot(MenuItemData currentRoot, int direction)
    {
        if (this.controller is null || this.MenuSource is null || this.MenuSource.Items.Count == 0)
        {
            return false;
        }

        if (direction == 0)
        {
            return false;
        }

        var count = this.MenuSource.Items.Count;
        var currentIndex = this.MenuSource.Items.IndexOf(currentRoot);

        if (currentIndex < 0)
        {
            return false;
        }

        var nextIndex = (currentIndex + direction + count) % count;
        var target = this.MenuSource.Items[nextIndex];
        var container = this.ResolveRootContainer(target);

        if (container is null)
        {
            return false;
        }

        this.controller.OnFocusRequested(this.CreateRootContext(), container, target, MenuInteractionActivationSource.KeyboardInput, openSubmenu: this.IsSubmenuOpen);
        return true;
    }

    private void HandleRootPointerActivation(MenuItem menuItem, MenuItemData menuItemData)
    {
        if (this.controller is null || !menuItemData.HasChildren)
        {
            return;
        }

        if (!this.IsSubmenuOpen || ReferenceEquals(menuItem, this.activeRootItem) || ReferenceEquals(menuItem, this.pendingRootItem))
        {
            return;
        }

        this.pendingRootItem = menuItem;
        this.controller.OnPointerEntered(this.CreateRootContext(), menuItem, menuItemData, true);
    }

    private void OnRootMenuItemSubmenuRequested(object? sender, MenuItemSubmenuEventArgs e)
    {
        if (this.controller is null || sender is not MenuItem menuItem)
        {
            return;
        }

        if (ReferenceEquals(menuItem, this.pendingRootItem))
        {
            return;
        }

        if (ReferenceEquals(menuItem, this.activeRootItem) && this.IsSubmenuOpen)
        {
            return;
        }

        this.pendingRootItem = menuItem;
        this.controller.OnFocusRequested(this.CreateRootContext(), menuItem, e.ItemData, MenuInteractionActivationSource.KeyboardInput, true);
    }

    private void OnRootMenuItemInvoked(object? sender, MenuItemInvokedEventArgs e)
    {
        if (this.controller is null || sender is not MenuItem menuItem)
        {
            return;
        }

        var source = this.controller.NavigationMode == MenuNavigationMode.KeyboardInput
            ? MenuInteractionActivationSource.KeyboardInput
            : MenuInteractionActivationSource.PointerInput;

        if (e.ItemData.HasChildren)
        {
            if (ReferenceEquals(menuItem, this.pendingRootItem))
            {
                return;
            }

            if (ReferenceEquals(menuItem, this.activeRootItem) && this.IsSubmenuOpen)
            {
                return;
            }

            this.pendingRootItem = menuItem;
            this.controller.OnFocusRequested(this.CreateRootContext(), menuItem, e.ItemData, source, true);
            return;
        }

        this.controller.OnInvokeRequested(this.CreateRootContext(), e.ItemData, source);
    }

    private void OpenRootSubmenuCore(MenuItemData root, FrameworkElement origin, MenuNavigationMode navigationMode)
    {
        if (this.MenuSource is null)
        {
            this.pendingRootItem = null;
            return;
        }

        if (!root.SubItems.Any())
        {
            this.pendingRootItem = null;
            return;
        }

        if (this.activeFlyout is not null)
        {
            this.activeFlyout.SuppressControllerDismissal = true;
            this.activeFlyout.Closed -= this.OnFlyoutClosed;
            this.activeFlyout.Hide();
            this.activeFlyout = null;
        }

        var submenuSource = new MenuSourceView(root.SubItems, this.MenuSource.Services);
        var flyout = new MenuFlyout
        {
            MenuSource = submenuSource,
            Placement = FlyoutPlacementMode.BottomEdgeAlignedLeft,
            OwnerNavigationMode = navigationMode,
            Controller = this.controller,
            RootSurface = this,
        };

        flyout.Closed += this.OnFlyoutClosed;

        var index = this.MenuSource.Items.IndexOf(root);
        this.OpenRootIndex = index >= 0 ? index : -1;
        this.IsSubmenuOpen = true;
        this.activeRootItem = origin as MenuItem;
        this.pendingRootItem = null;
        this.activeFlyout = flyout;

        var options = new FlyoutShowOptions
        {
            Placement = FlyoutPlacementMode.BottomEdgeAlignedLeft,
        };

        flyout.ShowAt(origin, options);
    }

    private void OnFlyoutClosed(object? sender, object e) => this.CloseActiveFlyout();

    private void CloseActiveFlyout()
    {
        if (this.activeFlyout is not null)
        {
            this.activeFlyout.SuppressControllerDismissal = true;
            this.activeFlyout.Closed -= this.OnFlyoutClosed;
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

        this.pendingRootItem = null;
        this.controller = newController;
    }

    private MenuInteractionContext CreateRootContext(IMenuInteractionSurface? columnSurface = null)
    {
        var surface = columnSurface ?? this.activeFlyout?.ColumnSurface;
        return MenuInteractionContext.ForRoot(this, surface);
    }

    private MenuItem? ResolveRootContainer(MenuItemData root)
    {
        if (this.rootItemsRepeater is null || this.MenuSource is null)
        {
            return null;
        }

        var index = this.MenuSource.Items.IndexOf(root);
        if (index < 0)
        {
            return null;
        }

        var element = this.rootItemsRepeater.TryGetElement(index) ?? this.rootItemsRepeater.GetOrCreateElement(index);
        return element as MenuItem;
    }
}
