// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.System;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Horizontal menu bar control that renders root <see cref="MenuItemData"/> instances and
///     materializes cascading submenus through an <see cref="ICascadedMenuHost"/>.
/// </summary>
[TemplatePart(Name = RootItemsPanelPart, Type = typeof(StackPanel))]
[SuppressMessage(
    "Microsoft.Design",
    "CA1001:TypesThatOwnDisposableFieldsShouldBeDisposable",
    Justification = "WinUI controls follow framework pattern of cleanup in Unloaded event and destructor, not IDisposable")]
public sealed partial class MenuBar : Control
{
    private const string RootItemsPanelPart = "PART_RootItemsPanel";

    private readonly Dictionary<MenuItemData, MenuItem> rootItemMap = [];
    private StackPanel? rootItemsPanel;
    private ObservableCollection<MenuItemData>? rootItemsCollection;
    private ICascadedMenuHost? activeHost;
    private Func<ICascadedMenuHost> hostFactory = static () => new PopupMenuHost();
    private CaptureInfo? capture;
    private MenuDismissKind lastHostDismissKind = MenuDismissKind.Programmatic;

    /// <summary>
    ///     Initializes a new instance of the <see cref="MenuBar"/> class.
    /// </summary>
    public MenuBar()
    {
        this.DefaultStyleKey = typeof(MenuBar);

        this.Loaded += this.OnLoaded;

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

        if (this.rootItemsPanel is not null)
        {
            this.DetachAllRootMenuItems();
        }

        this.rootItemsPanel = this.GetTemplateChild(RootItemsPanelPart) as StackPanel
            ?? throw new InvalidOperationException($"{nameof(MenuBar)} template must declare a StackPanel named '{RootItemsPanelPart}'.");

        this.RebuildRootItems();
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        this.ActualThemeChanged -= this.OnActualThemeChanged;
        this.activeHost?.Dispose();
        this.activeHost = null;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        this.ActualThemeChanged -= this.OnActualThemeChanged;
        this.ActualThemeChanged += this.OnActualThemeChanged;
        this.ApplyThemeToActiveHost();
    }

    private void OnActualThemeChanged(FrameworkElement sender, object args)
    {
        _ = args;
        this.ApplyThemeToActiveHost();
    }

    private void MenuItem_OnPointerPressed(object sender, PointerRoutedEventArgs e)
    {
        // We only care about root items that have children and are interactive.
        if (sender is not MenuItem { ItemData: { HasChildren: true, IsInteractive: true } itemData } item)
        {
            return;
        }

        // We need a valid menu source with an interaction controller to proceed.
        if (this.MenuSource is not { Services.InteractionController: { } controller })
        {
            return;
        }

        Debug.Assert(this.Items.Contains(itemData), "Expecting itemData to be part of the root items.");

        // Then we expand the item menu as usual...
        const MenuInteractionInputSource source = MenuInteractionInputSource.PointerInput;
        var result = controller.OnExpandRequested(this.CreateRootContext(), itemData, source);
        this.LogSubmenuRequested(itemData.Id, source, result);

        if (!result)
        {
            return;
        }

        this.CapturePointer(item, e.Pointer);
        e.Handled = true; // TODO: verify does not conflict with MenuItem pointer handling.
    }

    private void MenuItem_OnPointerEntered(object sender, PointerRoutedEventArgs e)
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

        var willExpand = controller.OnItemHoverStarted(this.CreateRootContext(), itemData);
        this.LogHoverStarted(itemData.Id, willExpand);
    }

    private void MenuItem_OnPointerExited(object sender, PointerRoutedEventArgs e)
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

    private void MenuItem_OnKeyDown(object sender, KeyRoutedEventArgs e)
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

        // Handle expansion via activation keys (Enter/Space).
        if (e.Key is VirtualKey.Enter or VirtualKey.Space && itemData.HasChildren && !itemData.IsExpanded)
        {
            var context = this.CreateRootContext();
            handled = controller.OnExpandRequested(context, itemData, MenuInteractionInputSource.KeyboardInput);
        }

        // Handle directional navigation (Up/Down/Left/Right). TODO: Home/End
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

    private void MenuItem_OnInvoked(object? sender, MenuItemInvokedEventArgs e)
    {
        if (this.MenuSource is not { Services.InteractionController: { } controller })
        {
            return;
        }

        this.LogItemInvoked(e.ItemData.Id, e.InputSource);
        controller.OnItemInvoked(this.CreateRootContext(), e.ItemData, e.InputSource);
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

        if (FocusManager.GetFocusedElement() is not FrameworkElement fe || !ReferenceEquals(fe, sender))
        {
            // Focus was lost/stolen before the GotFocus event could be processed.
            return;
        }

        if (sender is not MenuItem { ItemData: { } itemData } item)
        {
            this.LogEventAborted();
            return;
        }

        var fc = item.FocusState;
        Debug.Assert(fc != FocusState.Unfocused, "Expecting FocusState to not be Unfocused in GotFocus handler.");
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

    private void OnHostOpening(object? sender, EventArgs e)
    {
        if (sender is ICascadedMenuHost { Anchor: MenuItem { ItemData: { } itemData } } host)
        {
            itemData.IsExpanded = true;
            this.LogHostOpening(itemData.Id);
            this.ApplyThemeToHost(host);
        }
    }

    private void OnHostClosing(object? sender, MenuHostClosingEventArgs e)
        => this.lastHostDismissKind = e.Kind;

    private void OnHostClosed(object? sender, EventArgs e)
    {
        this.LogHostClosed();

        var dismissalKind = this.lastHostDismissKind;
        this.lastHostDismissKind = MenuDismissKind.Programmatic;

        if (this.MenuSource is { Items: { } items })
        {
            var expanded = items.FirstOrDefault(static item => item.IsExpanded);
            if (expanded is { })
            {
                expanded.IsExpanded = false;
            }
        }

        // Reset any existing pointer capture (defensive, in case Closed is called without Opened).
        this.ReleasePointer();

        if (this.DismissOnFlyoutDismissal)
        {
            this.RaiseDismissed(dismissalKind);
        }
    }

    private void Host_OnOpened(object? sender, EventArgs e)
    {
        if (this.MenuSource is not { Services.InteractionController: { } })
        {
            return;
        }

        // Reset any existing pointer capture, we don't need it anymore.
        this.ReleasePointer();

        this.ApplyThemeToActiveHost();

        var expanded = this.GetExpandedItem();
        Debug.Assert(expanded is not null, "Expecting an expanded item if the host is open.");
        this.LogHostOpened(expanded);
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
            host.Closing += this.OnHostClosing;
            host.Opening += this.OnHostOpening;
            host.Opened += this.Host_OnOpened;
            this.activeHost = host;
        }

        this.activeHost.RootSurface = this;
        return this.activeHost;
    }

    private void ApplyThemeToActiveHost()
    {
        if (this.activeHost is null)
        {
            return;
        }

        this.ApplyThemeToHost(this.activeHost);
    }

    private void ApplyThemeToHost(ICascadedMenuHost host)
    {
        try
        {
            if (host.RootElement is FrameworkElement element)
            {
                element.RequestedTheme = this.ActualTheme;
            }
        }
        catch (InvalidOperationException)
        {
            // Host presenter may not be created yet; ignore until it is available.
        }
    }

    private void SetRootItemsCollection(ObservableCollection<MenuItemData>? newCollection)
    {
        if (ReferenceEquals(this.rootItemsCollection, newCollection))
        {
            return;
        }

        if (this.rootItemsCollection is not null)
        {
            this.rootItemsCollection.CollectionChanged -= this.OnRootItemsCollectionChanged;
        }

        this.rootItemsCollection = newCollection;

        if (this.rootItemsCollection is not null)
        {
            this.rootItemsCollection.CollectionChanged += this.OnRootItemsCollectionChanged;
        }
    }

    private void OnRootItemsCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        switch (e.Action)
        {
            case NotifyCollectionChangedAction.Add:
                this.InsertMenuItems(e.NewStartingIndex, e.NewItems);
                break;

            case NotifyCollectionChangedAction.Remove:
                this.RemoveMenuItems(e.OldItems);
                break;

            case NotifyCollectionChangedAction.Replace:
                this.RemoveMenuItems(e.OldItems);
                this.InsertMenuItems(e.NewStartingIndex, e.NewItems);
                break;

            case NotifyCollectionChangedAction.Move:
                this.MoveMenuItems(e.NewStartingIndex, e.NewItems);
                break;

            case NotifyCollectionChangedAction.Reset:
                this.RebuildRootItems();
                break;
        }
    }

    private void RebuildRootItems()
    {
        this.DetachAllRootMenuItems();

        if (this.rootItemsPanel is null)
        {
            return;
        }

        this.rootItemsPanel.Children.Clear();

        if (this.MenuSource is not { Items: { } items })
        {
            return;
        }

        foreach (var data in items)
        {
            var menuItem = this.CreateRootMenuItem(data);
            this.rootItemsPanel.Children.Add(menuItem);
        }
    }

    private MenuItem CreateRootMenuItem(MenuItemData data)
    {
        var menuItem = new MenuItem
        {
            ItemData = data,
        };

        this.AttachRootMenuItem(menuItem);
        return menuItem;
    }

    private void InsertMenuItems(int index, IList? items)
    {
        if (this.rootItemsPanel is null || items is null || items.Count == 0)
        {
            return;
        }

        var insertionIndex = index >= 0 ? index : this.rootItemsPanel.Children.Count;

        foreach (var item in items)
        {
            if (item is not MenuItemData data)
            {
                continue;
            }

            var menuItem = this.CreateRootMenuItem(data);

            if (insertionIndex < 0 || insertionIndex > this.rootItemsPanel.Children.Count)
            {
                this.rootItemsPanel.Children.Add(menuItem);
            }
            else
            {
                this.rootItemsPanel.Children.Insert(insertionIndex, menuItem);
                insertionIndex++;
            }
        }
    }

    private void RemoveMenuItems(IList? items)
    {
        if (items is null || items.Count == 0)
        {
            return;
        }

        foreach (var item in items)
        {
            if (item is not MenuItemData data)
            {
                continue;
            }

            if (this.rootItemMap.TryGetValue(data, out var menuItem))
            {
                _ = this.rootItemsPanel?.Children.Remove(menuItem);
                this.DetachRootMenuItem(menuItem);
            }
        }
    }

    private void MoveMenuItems(int newIndex, IList? items)
    {
        if (this.rootItemsPanel is null || items is null || items.Count == 0)
        {
            return;
        }

        foreach (var item in items)
        {
            if (item is not MenuItemData data)
            {
                continue;
            }

            if (!this.rootItemMap.TryGetValue(data, out var menuItem))
            {
                continue;
            }

            var currentIndex = this.rootItemsPanel.Children.IndexOf(menuItem);
            if (currentIndex < 0)
            {
                continue;
            }

            this.rootItemsPanel.Children.RemoveAt(currentIndex);

            var insertIndex = newIndex;
            if (currentIndex < insertIndex)
            {
                insertIndex--;
            }

            if (insertIndex < 0)
            {
                insertIndex = 0;
            }

            if (insertIndex > this.rootItemsPanel.Children.Count)
            {
                insertIndex = this.rootItemsPanel.Children.Count;
            }

            this.rootItemsPanel.Children.Insert(insertIndex, menuItem);
        }
    }

    private void AttachRootMenuItem(MenuItem menuItem)
    {
        if (menuItem.ItemData is null)
        {
            return;
        }

        menuItem.MenuSource = this.MenuSource;
        menuItem.ShowSubmenuGlyph = false;

        this.rootItemMap[menuItem.ItemData] = menuItem;

        menuItem.Invoked += this.MenuItem_OnInvoked;
        menuItem.KeyDown += this.MenuItem_OnKeyDown;
        menuItem.PointerEntered += this.MenuItem_OnPointerEntered;
        menuItem.PointerExited += this.MenuItem_OnPointerExited;
        menuItem.PointerPressed += this.MenuItem_OnPointerPressed;
        menuItem.GettingFocus += this.MenuItem_OnGettingFocus;
        menuItem.GotFocus += this.MenuItem_OnGotFocus;
        menuItem.LostFocus += this.MenuItem_OnLostFocus;
        menuItem.PreviewKeyDown += this.MenuItem_OnPreviewKeyDown;
    }

    private void DetachRootMenuItem(MenuItem menuItem)
    {
        if (this.capture?.TryGetCaptor(out var captor) == true && ReferenceEquals(captor, menuItem))
        {
            this.ReleasePointer();
        }

        menuItem.Invoked -= this.MenuItem_OnInvoked;
        menuItem.KeyDown -= this.MenuItem_OnKeyDown;
        menuItem.PointerEntered -= this.MenuItem_OnPointerEntered;
        menuItem.PointerExited -= this.MenuItem_OnPointerExited;
        menuItem.PointerPressed -= this.MenuItem_OnPointerPressed;
        menuItem.GettingFocus -= this.MenuItem_OnGettingFocus;
        menuItem.GotFocus -= this.MenuItem_OnGotFocus;
        menuItem.LostFocus -= this.MenuItem_OnLostFocus;
        menuItem.PreviewKeyDown -= this.MenuItem_OnPreviewKeyDown;

        if (menuItem.ItemData is { } data && this.rootItemMap.TryGetValue(data, out var existing) && ReferenceEquals(existing, menuItem))
        {
            _ = this.rootItemMap.Remove(data);
        }

        menuItem.ShowSubmenuGlyph = true;
        menuItem.MenuSource = null;
    }

    private void DetachAllRootMenuItems()
    {
        if (this.rootItemMap.Count == 0)
        {
            return;
        }

        foreach (var menuItem in this.rootItemMap.Values.ToList())
        {
            this.DetachRootMenuItem(menuItem);
        }

        this.rootItemsPanel?.Children.Clear();
    }

    private MenuItem? ResolveToItem(MenuItemData root)
    {
        if (this.rootItemMap.TryGetValue(root, out var menuItem))
        {
            return menuItem;
        }

        if (this.rootItemsPanel is null)
        {
            return null;
        }

        foreach (var child in this.rootItemsPanel.Children)
        {
            if (child is MenuItem item && ReferenceEquals(item.ItemData, root))
            {
                this.rootItemMap[root] = item;
                return item;
            }
        }

        return null;
    }

    // We capture the pointer and keep it until the flyout if opened, to avoid
    // hover events on other root items, WinUI playing with our pointer, etc.
    private void CapturePointer(MenuItem captor, Pointer pointer)
    {
        if (this.capture is not null)
        {
            return;
        }

        if (captor.CapturePointer(pointer))
        {
            this.capture = new CaptureInfo(captor, pointer.PointerId);
            this.LogCaptureSuccess();
        }
        else
        {
            this.LogCaptureFailed();
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "cannot do anything about the exceptions")]
    private void ReleasePointer()
    {
        if (this.capture?.TryGetCaptor(out var captor) == true)
        {
            _ = this.DispatcherQueue.TryEnqueue(() =>
            {
                try
                {
                    captor?.ReleasePointerCaptures();
                }
                catch
                {
                }
            });
        }

        this.capture = null;
    }

    private readonly struct CaptureInfo(MenuItem captor, uint pointerId)
    {
        public readonly WeakReference<MenuItem> CaptorRef = new(captor);
        public readonly uint PointerId = pointerId;

        public bool TryGetCaptor(out MenuItem? captor) => this.CaptorRef.TryGetTarget(out captor);
    }
}
