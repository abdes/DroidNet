// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.System;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Presents a single vertical column of <see cref="MenuItem"/> controls.
/// </summary>
[TemplatePart(Name = ItemsHostPart, Type = typeof(StackPanel))]
public sealed partial class ColumnPresenter : Control
{
    private const string ItemsHostPart = "PART_ItemsHost";

    private StackPanel? itemsHost;
    private INotifyCollectionChanged? observableItems;
    private (int, MenuNavigationMode)? deferredFocusPosition;

    /// <summary>
    ///     Initializes a new instance of the <see cref="ColumnPresenter"/> class.
    /// </summary>
    public ColumnPresenter()
    {
        this.DefaultStyleKey = typeof(ColumnPresenter);
        this.LogCreated();
    }

    /// <summary>
    ///     Gets or sets the owning <see cref="CascadedColumnsPresenter"/> responsible for column-level surface operations.
    /// </summary>
    internal CascadedColumnsPresenter? OwnerPresenter { get; set; }

    private List<MenuItem> MenuItems => this.itemsHost?.Children.OfType<MenuItem>().ToList() ?? [];

    /// <summary>
    ///     Gets the menu item that is adjacent to the specified <paramref name="itemData"/> within the column,
    ///     following the requested <paramref name="direction"/>. The search only considers interactive items
    ///     (non-separator and enabled items) in the underlying <see cref="ItemsSource"/> collection.
    /// </summary>
    /// <param name="itemData">
    ///     The reference menu item from which to compute the adjacent item. The item must
    ///     be an instance contained in the current <see cref="ItemsSource"/> sequence.
    /// </param>
    /// <param name="direction">
    ///     The navigation direction that determines whether the adjacent item is located
    ///     before or after the <paramref name="itemData"/>.
    /// </param>
    /// <param name="wrap">
    ///     If <see langword="true"/>, the search wraps around the start/end of the list when
    ///     computing the adjacent index; otherwise the result is clipped to the first/last interactive item.
    /// </param>
    /// <returns>
    ///     The adjacent <see cref="MenuItemData"/> instance determined by the given parameters.
    /// </returns>
    /// <exception cref="ArgumentException">
    ///     Thrown when <paramref name="itemData"/> is not part of the current
    ///     <see cref="ItemsSource"/> sequence.
    /// </exception>
    public MenuItemData GetAdjacentItem(MenuItemData itemData, MenuNavigationDirection direction, bool wrap = true)
    {
        this.LogGetAdjacentItem(itemData.Id, direction, wrap);
        Debug.Assert(this.ItemsSource is not null, "Expecting ItemsSource to be not null");
        var items = this.ItemsSource.OfType<MenuItemData>().Where(it => it.IsInteractive).ToList();
        var index = items.FindIndex(item => ReferenceEquals(item, itemData));
        if (index < 0)
        {
            throw new ArgumentException("The provided item is not part of the menu source.", nameof(itemData));
        }

        var step = direction switch
        {
            MenuNavigationDirection.Left or MenuNavigationDirection.Up => -1,
            MenuNavigationDirection.Right or MenuNavigationDirection.Down => 1,
            _ => 0,
        };

        var count = items.Count;
        Debug.Assert(count > 0, "Expecting MenuSource.Items to not be empty");

        var nextIndex = wrap
            ? (index + step + count) % count
            : Math.Clamp(index + step, 0, count - 1);

        return items[nextIndex];
    }

    /// <summary>
    ///     Returns the first item in the <see cref="ItemsSource"/> that is currently marked as expanded.
    /// </summary>
    /// <remarks>
    ///     If the <see cref="ItemsSource"/> is <see langword="null"/> or no item has <see cref="MenuItemData.IsExpanded"/>
    ///     set to <see langword="true"/>, the method returns <see langword="null"/>.
    /// </remarks>
    /// <returns>
    ///     The expanded <see cref="MenuItemData"/>, or <see langword="null"/> if none is expanded or the source
    ///     is not available.
    /// </returns>
    public MenuItemData? GetExpandedItem()
    {
        if (this.ItemsSource is not IEnumerable<MenuItemData> { } items)
        {
            return null;
        }

        var result = items.OfType<MenuItemData>().FirstOrDefault(item => item.IsExpanded);
        this.LogGetExpandedItem(result?.Id);
        return result;
    }

    /// <summary>
    ///     Returns the <see cref="MenuItemData"/> that currently owns keyboard focus within this column,
    ///     or <see langword="null"/> if no item in this column is focused.
    /// </summary>
    /// <remarks>
    ///     The method resolves focus using the <see cref="FocusManager"/> for the current <see cref="XamlRoot"/>.
    ///     If the focused element is a <see cref="MenuItem"/> whose <see cref="MenuItem.ItemData"/> belongs
    ///     to the present <see cref="ItemsSource"/>, that data object is returned. Otherwise <see langword="null"/> is returned.
    /// </remarks>
    /// <returns>
    ///     The focused <see cref="MenuItemData"/>, or <see langword="null"/> when no focused item belongs to the
    ///     current items source.
    /// </returns>
    public MenuItemData? GetFocusedItem()
    {
        if (this.ItemsSource is not IEnumerable<MenuItemData> { } items)
        {
            return null;
        }

        if (FocusManager.GetFocusedElement(this.XamlRoot) is MenuItem { ItemData: MenuItemData data }
            && items.Contains(data))
        {
            this.LogGetFocusedItem(data.Id);
            return data;
        }

        this.LogGetFocusedItem(itemId: null);
        return null;
    }

    /// <summary>
    ///     Attempts to focus the supplied menu item within the column.
    /// </summary>
    /// <param name="menuItem">The item to focus.</param>
    /// <param name="navMode">The navigation mode guiding the focus action.</param>
    /// <remarks>
    ///      Expects the <see cref="ColumnPresenter"/> to be fully realized.
    ///      Use <see cref="FocusFirstItem"/> or <see cref="FocusItemAt(int, MenuNavigationMode)"/> to request
    ///      focus at a specificposition, which may be deferred until the presenter is fully realized.
    /// </remarks>
    /// <returns>
    ///     <see langword="true"/> if the specified item was focused successfully; otherwise <see langword="false"/>.
    /// </returns>
    internal bool FocusItem(MenuItemData menuItem, MenuNavigationMode navMode)
    {
        if (this.ItemsSource is not { } items)
        {
            this.LogFocusItemNoItemsSource(menuItem, navMode);
            return false;
        }

        var position = items.OfType<MenuItemData>().ToList().FindIndex(item => ReferenceEquals(item, menuItem));
        if (position < 0)
        {
            this.LogFocusItemNotFound(menuItem.Id, navMode);
            return false;
        }

        return this.FocusItemAt(position, navMode);
    }

    /// <summary>
    ///     Attempts to focus the supplied menu item within the column.
    /// </summary>
    /// <param name="position">Position, in the column, of the item to focus.</param>
    /// <param name="navMode">The navigation mode guiding the focus action.</param>
    /// <returns>
    ///     <see langword="true"/> if the item at the specified position received focus; otherwise <see langword="false"/>.
    /// </returns>
    internal bool FocusItemAt(int position, MenuNavigationMode navMode)
    {
        if (this.itemsHost is not { })
        {
            this.deferredFocusPosition = (position, navMode);
            this.LogFocusDeferred(position, navMode);
            return false;
        }

        if (position < 0 || position >= this.MenuItems.Count)
        {
            this.LogFocusItemNotFound(position, navMode);
            return false;
        }

        var focusState = navMode.ToFocusState();

        var target = this.MenuItems[position];
        Debug.Assert(target.ItemData is not null, "Expecting MenuItem with valid ItemData in FocusItemAt");

        if (!target.IsFocusable)
        {
            this.LogItemNotFocusable(target.ItemData.Id, position, navMode);
            return false;
        }

        var result = target.Focus(focusState);
        this.LogFocusItemResult(target.ItemData.Id, position, navMode, result);
        return result;
    }

    /// <summary>
    ///     Focuses the first focusable item in the column.
    /// </summary>
    /// <param name="navMode">The navigation mode guiding the focus action.</param>
    /// <returns>
    ///     <see langword="true"/> if the first item was focused successfully; otherwise <see langword="false"/>.
    /// </returns>
    internal bool FocusFirstItem(MenuNavigationMode navMode)
        => this.FocusItemAt(0, navMode);

    /// <inheritdoc />
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        this.itemsHost = this.GetTemplateChild(ItemsHostPart) as StackPanel
            ?? throw new InvalidOperationException($"{nameof(ColumnPresenter)} template must contain a StackPanel named '{ItemsHostPart}'.");

        this.PopulateItems();

        this.itemsHost.Loaded -= this.ItemsHost_OnLoaded;
        this.itemsHost.Loaded += this.ItemsHost_OnLoaded;

        // Setup keyboard navigation handler for initiial navigation (when no item is focused yet).
        this.KeyDown += this.HandleInitialNavigation;
    }

    private void ItemsHost_OnLoaded(object sender, RoutedEventArgs e)
    {
        // Now that the template is applied, process any deferred focus request, otherwise focus the first item by default.
        if (this.deferredFocusPosition is (int position, MenuNavigationMode navMode))
        {
            this.LogDeferredFocusProcess(position, navMode);

            _ = this.FocusItemAt(position, navMode);
            this.deferredFocusPosition = null;
        }
    }

    private void PopulateItems()
    {
        if (this.itemsHost is null)
        {
            return;
        }

        if (this.ItemsSource is null)
        {
            return;
        }

        this.LogPopulateItems();

        foreach (var itemData in this.ItemsSource.OfType<MenuItemData>())
        {
            var menuItem = new MenuItem { ItemData = itemData, MenuSource = this.MenuSource };
            HookMenuItem(menuItem);
            this.itemsHost.Children.Add(menuItem);
        }

        void HookMenuItem(MenuItem item)
        {
            item.Invoked += this.MenuItem_OnInvoked;
            item.KeyDown += this.MenuItem_OnKeyDown;
            item.PointerEntered += this.MenuItem_OnPointerEntered;
            item.GettingFocus += this.MenuItem_OnGettingFocus;
            item.GotFocus += this.MenuItem_OnGotFocus;
        }
    }

    private void ClearItems()
    {
        if (this.itemsHost is null)
        {
            return;
        }

        this.LogClearItems();

        foreach (var child in this.itemsHost.Children.OfType<MenuItem>())
        {
            UnhookMenuItem(child);
        }

        this.itemsHost.Children.Clear();

        void UnhookMenuItem(MenuItem item)
        {
            item.Invoked -= this.MenuItem_OnInvoked;
            item.KeyDown -= this.MenuItem_OnKeyDown;
            item.PointerEntered -= this.MenuItem_OnPointerEntered;
            item.GettingFocus -= this.MenuItem_OnGettingFocus;
            item.GotFocus -= this.MenuItem_OnGotFocus;
        }
    }

    /// <summary>
    ///     Handles the key down event to catch the first navigation (Up/Down/Right/Left) action after opening.
    /// </summary>
    private void HandleInitialNavigation(object sender, KeyRoutedEventArgs e)
    {
        if (this.MenuSource is not { Services.InteractionController: { } controller })
        {
            return;
        }

        var handled = false;

        if (e.Key is Windows.System.VirtualKey.Tab or Windows.System.VirtualKey.Down)
        {
            this.FocusFirstItem(MenuNavigationMode.KeyboardInput);
            handled = true;
        }
        else if (e.Key is Windows.System.VirtualKey.Up)
        {
            // TODO: this.FocusLastItem(MenuLevel.First, MenuNavigationMode.KeyboardInput);
            handled = true;
        }
        else if (e.Key is Windows.System.VirtualKey.Right or Windows.System.VirtualKey.Left)
        {
            var context = this.CreateCurrentContext();
            if (context.RootSurface is null)
            {
                this.FocusFirstItem(MenuNavigationMode.KeyboardInput);
                handled = true;
            }
            else
            {
                var direction = e.Key is Windows.System.VirtualKey.Right
                    ? MenuNavigationDirection.Right
                    : MenuNavigationDirection.Left;
                var expanded = context.RootSurface.GetExpandedItem();
                if (expanded is { })
                {
                    controller.OnDirectionalNavigation(context, expanded, direction, MenuInteractionInputSource.KeyboardInput);
                    handled = true;
                }
            }
        }

        if (handled)
        {
            e.Handled = true;
            this.KeyDown -= this.HandleInitialNavigation; // Only handle the first navigation key.
        }
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
            var context = this.CreateCurrentContext();
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
            var context = this.CreateCurrentContext();
            handled = controller.OnDirectionalNavigation(context, itemData, direction.Value, MenuInteractionInputSource.KeyboardInput);
        }

        if (handled)
        {
            e.Handled = true;
        }
    }

    private void MenuItem_OnPointerEntered(object sender, PointerRoutedEventArgs e)
    {
        if (this.Controller is not { } controller)
        {
            return;
        }

        if (sender is not MenuItem { ItemData: { } itemData })
        {
            return;
        }

        var context = this.CreateCurrentContext();
        _ = controller.OnItemHoverStarted(context, itemData);
    }

    private void MenuItem_OnInvoked(object? sender, MenuItemInvokedEventArgs e)
        => this.ItemInvoked?.Invoke(this, e);

    private void MenuItem_OnGettingFocus(UIElement sender, GettingFocusEventArgs args)
    {
        if (this.MenuSource is not { Services.InteractionController: { } controller })
        {
            return;
        }

        if (sender is not MenuItem { ItemData: { } })
        {
            this.LogGettingFocusAborted();
            return;
        }

        // Relay this event to the menu controller so it can retrieve the previously focused element.
        controller.OnGettingFocus(this.CreateCurrentContext(), args.OldFocusedElement);
    }

    private void MenuItem_OnGotFocus(object sender, RoutedEventArgs e)
    {
        if (sender is not MenuItem item || item.ItemData is null)
        {
            return;
        }

        this.LogGotFocus(item.ItemData.Id, item.FocusState);
        if (item.FocusState != FocusState.Keyboard)
        {
            return;
        }

        if (this.Controller is not { } controller)
        {
            return;
        }

        var context = this.CreateCurrentContext();
        controller.OnItemGotFocus(context, item.ItemData, MenuInteractionInputSource.KeyboardInput);
    }

    private MenuItem? GetFocusedMenuItem()
        => FocusManager.GetFocusedElement(this.XamlRoot) is MenuItem focused && focused.ItemData is not null
            ? focused
            : this.itemsHost?.Children.OfType<MenuItem>().FirstOrDefault(item => item.FocusState == FocusState.Keyboard);

    private MenuInteractionContext CreateContext(int columnLevel)
    {
        var owner = this.OwnerPresenter ?? throw new InvalidOperationException("Owner presenter is not set for MenuColumnPresenter.");
        return MenuInteractionContext.ForColumn(new MenuLevel(columnLevel), owner, owner.RootSurface);
    }

    private MenuInteractionContext CreateCurrentContext() => this.CreateContext(this.ColumnLevel);

    private MenuItem? GetActiveOrFirstFocusableItem()
    {
        if (this.itemsHost is null)
        {
            return null;
        }

        var active = this.itemsHost.Children.OfType<MenuItem>().FirstOrDefault(item => item.ItemData?.IsExpanded == true);
        if (active is not null)
        {
            return active;
        }

        var fallback = this.itemsHost.Children.OfType<MenuItem>().FirstOrDefault(item => item.ItemData is { IsEnabled: true } data && !data.IsSeparator);
        return fallback;
    }
}
