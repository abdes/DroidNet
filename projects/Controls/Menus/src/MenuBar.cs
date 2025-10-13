// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.System;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Horizontal menu bar control that renders root <see cref="MenuItemData"/> instances and
///     materializes cascading submenus through an <see cref="ICascadedMenuHost"/>.
/// </summary>
[TemplatePart(Name = RootItemsRepeaterPart, Type = typeof(ItemsRepeater))]
[SuppressMessage(
    "Microsoft.Design",
    "CA1001:TypesThatOwnDisposableFieldsShouldBeDisposable",
    Justification = "WinUI controls follow framework pattern of cleanup in Unloaded event and destructor, not IDisposable")]
public sealed partial class MenuBar : Control
{
    private const string RootItemsRepeaterPart = "PART_RootItemsRepeater";

    private ItemsRepeater? rootItemsRepeater;
    private ICascadedMenuHost? activeHost;
    private Func<ICascadedMenuHost> hostFactory = static () => new FlyoutMenuHost();

    /// <summary>
    ///     Initializes a new instance of the <see cref="MenuBar"/> class.
    /// </summary>
    public MenuBar()
    {
        this.DefaultStyleKey = typeof(MenuBar);

        this.GettingFocus += (sender, args) =>
        {
            if (this.MenuSource is not { Services.InteractionController: { } controller })
            {
                return;
            }

            // Notify the interaction controller that a root item is losing focus.
            controller.OnGettingFocus(this.CreateRootContext(), args.OldFocusedElement);
        };

        this.Unloaded += this.OnUnloaded;
    }

    /// <summary>
    ///     Gets or sets the factory used to create <see cref="ICascadedMenuHost"/> instances for this bar.
    /// </summary>
    /// <remarks>
    ///     The supplied factory must return a fresh host each time it is invoked. Setting this property after a host has
    ///     already been created will not replace the existing instance; call <see cref="IDisposable.Dispose"/>
    ///     manually before changing the factory when necessary.
    /// </remarks>
    internal Func<ICascadedMenuHost> HostFactory
    {
        get => this.hostFactory;
        set => this.hostFactory = value ?? throw new ArgumentNullException(nameof(value));
    }

    private IReadOnlyList<MenuItemData> Items => this.MenuSource?.Items
        ?? throw new InvalidOperationException("MenuSource is not set.");

    /// <inheritdoc />
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        if (this.rootItemsRepeater is not null)
        {
            this.rootItemsRepeater.ElementPrepared -= this.OnItemPrepared;
            this.rootItemsRepeater.ElementClearing -= this.OnItemClearing;
        }

        this.rootItemsRepeater = this.GetTemplateChild(RootItemsRepeaterPart) as ItemsRepeater
            ?? throw new InvalidOperationException($"{nameof(MenuBar)} template must declare an ItemsRepeater named '{RootItemsRepeaterPart}'.");

        this.rootItemsRepeater.ItemsSource = this.MenuSource?.Items;

        this.rootItemsRepeater.ElementPrepared += this.OnItemPrepared;
        this.rootItemsRepeater.ElementClearing += this.OnItemClearing;
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        this.activeHost?.Dispose();
        this.activeHost = null;
    }

    private void OnItemPrepared(ItemsRepeater sender, ItemsRepeaterElementPreparedEventArgs args)
    {
        if (args.Element is not MenuItem menuItem || menuItem.ItemData is null)
        {
            return;
        }

        menuItem.ShowSubmenuGlyph = false;

        menuItem.Invoked += this.MenuItem_OnInvoked;
        menuItem.SubmenuRequested += this.MenuItem_OnSubmenuRequested;
        menuItem.HoverStarted += this.MenuItem_OnHoverStarted;
        menuItem.HoverEnded += this.MenuItem_OnHoverEnded;
        menuItem.GettingFocus += this.MenuItem_OnGettingFocus;
        menuItem.GotFocus += this.MenuItem_OnGotFocus;
        menuItem.LostFocus += this.MenuItem_OnLostFocus;

        // Implement PreviewKeyDown to handle Left/Right arrow navigation between root items and submenu opening.
        menuItem.PreviewKeyDown += this.MenuItem_OnPreviewKeyDown;
    }

    private void OnItemClearing(ItemsRepeater sender, ItemsRepeaterElementClearingEventArgs args)
    {
        if (args.Element is not MenuItem menuItem)
        {
            return;
        }

        menuItem.ShowSubmenuGlyph = true;

        menuItem.Invoked -= this.MenuItem_OnInvoked;
        menuItem.SubmenuRequested -= this.MenuItem_OnSubmenuRequested;
        menuItem.HoverStarted -= this.MenuItem_OnHoverStarted;
        menuItem.HoverEnded -= this.MenuItem_OnHoverEnded;
        menuItem.GettingFocus -= this.MenuItem_OnGettingFocus;
        menuItem.GotFocus -= this.MenuItem_OnGotFocus;
        menuItem.LostFocus -= this.MenuItem_OnLostFocus;
        menuItem.PreviewKeyDown -= this.MenuItem_OnPreviewKeyDown;
    }

    private void MenuItem_OnRootRadioGroupSelectionRequested(object? sender, MenuItemRadioGroupEventArgs e)
    {
        if (this.MenuSource is { Services.InteractionController: { } controller })
        {
            controller.OnRadioGroupSelectionRequested(e.ItemData);
            return;
        }

        this.MenuSource?.Services.HandleGroupSelection(e.ItemData);
    }

    private void MenuItem_OnInvoked(object? sender, MenuItemInvokedEventArgs e)
    {
        if (this.MenuSource is not { Services.InteractionController: { } controller })
        {
            return;
        }

        this.LogItemInvoked(e.ItemData.Id, e.InputSource);
        controller.OnItemInvoked(this.CreateRootContext(), e.ItemData, e.InputSource);
    }

    private void MenuItem_OnHoverStarted(object? sender, MenuItemHoverEventArgs e)
    {
        if (this.MenuSource is not { Services.InteractionController: { } controller })
        {
            return;
        }

        if (sender is not MenuItem { ItemData: { } itemData })
        {
            this.LogEventAborted();
            return;
        }

        this.LogHoverStarted(itemData.Id);
        controller.OnItemHoverStarted(this.CreateRootContext(), itemData);
    }

    private void MenuItem_OnHoverEnded(object? sender, MenuItemHoverEventArgs e)
    {
        if (this.MenuSource is not { Services.InteractionController: { } controller })
        {
            return;
        }

        if (sender is not MenuItem { ItemData: { } itemData })
        {
            this.LogEventAborted();
            return;
        }

        this.LogHoverEnded(itemData.Id);
        controller.OnItemHoverEnded(this.CreateRootContext(), itemData);
    }

    private void MenuItem_OnGettingFocus(UIElement sender, GettingFocusEventArgs args)
    {
        if (this.MenuSource is not { Services.InteractionController: { } controller })
        {
            return;
        }

        if (sender is not MenuItem { ItemData: { } })
        {
            this.LogEventAborted();
            return;
        }

        // Relay this event to the menu controller so it can retrieve the previously focused element.
        controller.OnGettingFocus(this.CreateRootContext(), args.OldFocusedElement);
    }

    private void MenuItem_OnGotFocus(object sender, RoutedEventArgs e)
    {
        if (this.MenuSource is not { Services.InteractionController: { } controller })
        {
            return;
        }

        if (sender is not MenuItem { ItemData: { } itemData })
        {
            this.LogEventAborted();
            return;
        }

        var uie = e.OriginalSource as UIElement;
        var fc = uie?.FocusState;

        var inputSource = fc switch
        {
            FocusState.Keyboard => MenuInteractionInputSource.KeyboardInput,
            FocusState.Pointer => MenuInteractionInputSource.PointerInput,
            FocusState.Programmatic => MenuInteractionInputSource.Programmatic,
            _ => MenuInteractionInputSource.Programmatic,
        };
        this.LogGotFocus(itemData.Id, inputSource);
        controller.OnItemGotFocus(this.CreateRootContext(), itemData, inputSource);
    }

    private void MenuItem_OnLostFocus(object? sender, RoutedEventArgs e)
    {
        if (this.MenuSource is not { Services.InteractionController: { } controller })
        {
            return;
        }

        if (sender is not MenuItem { ItemData: { } itemData })
        {
            this.LogEventAborted();
            return;
        }

        this.LogLostFocus(itemData.Id);
        controller.OnItemLostFocus(this.CreateRootContext(), itemData);
    }

    private void MenuItem_OnPreviewKeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (sender is not MenuItem { ItemData: { } itemData })
        {
            return;
        }

        if (this.MenuSource is not { Services.InteractionController: { } controller })
        {
            return;
        }

        var handled = false;
        if (e.Key == VirtualKey.Escape)
        {
            MenuInteractionContext context;
            if (itemData.IsExpanded)
            {
                Debug.Assert(this.activeHost is { IsOpen: true }, "Expecting activeHost to be open if itemData.IsExpanded is true.");
                context = MenuInteractionContext.ForColumn(MenuLevel.First, this.activeHost, this);
            }
            else
            {
                context = MenuInteractionContext.ForRoot(this);
            }

            handled = controller.OnDismissRequested(context, MenuDismissKind.KeyboardInput);
            if (handled)
            {
                e.Handled = true;
            }

            return;
        }

        MenuNavigationDirection? direction = e.Key switch
        {
            VirtualKey.Left => MenuNavigationDirection.Left,
            VirtualKey.Right => MenuNavigationDirection.Right,
            VirtualKey.Up => MenuNavigationDirection.Up,
            VirtualKey.Down => MenuNavigationDirection.Down,
            _ => null,
        };

        if (direction is not null)
        {
            var context = this.CreateRootContext();
            handled = controller.OnDirectionalNavigation(context, itemData, direction.Value, MenuInteractionInputSource.KeyboardInput);
        }

        if (handled)
        {
            e.Handled = true;
        }
    }

    private void MenuItem_OnSubmenuRequested(object? sender, MenuItemSubmenuEventArgs e)
    {
        if (this.MenuSource is not { Services.InteractionController: { } controller })
        {
            return;
        }

        this.LogSubmenuRequested(e.ItemData.Id, e.InputSource);
        controller.OnExpandRequested(this.CreateRootContext(), e.ItemData, e.InputSource);
    }

    private void OnHostClosed(object? sender, EventArgs e)
    {
        this.LogHostClosed();

        if (this.MenuSource is not { Items: { } items })
        {
            return;
        }

        var expanded = items.FirstOrDefault(static item => item.IsExpanded);
        if (expanded is { })
        {
            expanded.IsExpanded = false;
        }
    }

    private MenuInteractionContext CreateRootContext(ICascadedMenuSurface? columnSurface = null)
    {
        var surface = columnSurface;
        if (surface is null && this.activeHost is { IsOpen: true } host)
        {
            surface = host.Surface;
        }

        return MenuInteractionContext.ForRoot(this, surface);
    }

    private ICascadedMenuHost EnsureHost()
    {
        if (this.activeHost is null)
        {
            var host = this.hostFactory();
            host.Closed += this.OnHostClosed;
            host.Opening += this.OnHostOpening;
            this.activeHost = host;
        }

        this.activeHost.RootSurface = this;
        return this.activeHost;
    }

    private void OnHostOpening(object? sender, EventArgs e)
    {
        if (sender is ICascadedMenuHost { Anchor: MenuItem { ItemData: { } itemData } })
        {
            this.LogHostOpening(itemData.Id);
            itemData.IsExpanded = true;
        }
    }

    private MenuItem? ResolveToItem(MenuItemData root)
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
