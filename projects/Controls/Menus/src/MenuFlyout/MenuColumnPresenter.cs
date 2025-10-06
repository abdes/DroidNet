// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using System.Collections.Specialized;
using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.System;

namespace DroidNet.Controls;

/// <summary>
///     Presents a single vertical column of <see cref="MenuItem"/> controls. The presenter bridges the
///     templated visual tree with <see cref="MenuInteractionController"/> so parent controls can keep their
///     logic agnostic of individual item wiring.
/// </summary>
[TemplatePart(Name = ItemsHostPart, Type = typeof(StackPanel))]
public sealed partial class MenuColumnPresenter : Control
{
    /// <summary>
    ///     Identifies the <see cref="Controller"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty ControllerProperty = DependencyProperty.Register(
        nameof(Controller),
        typeof(MenuInteractionController),
        typeof(MenuColumnPresenter),
        new PropertyMetadata(defaultValue: null, OnControllerChanged));

    /// <summary>
    ///     Identifies the <see cref="ItemsSource"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty ItemsSourceProperty = DependencyProperty.Register(
        nameof(ItemsSource),
        typeof(IEnumerable),
        typeof(MenuColumnPresenter),
        new PropertyMetadata(defaultValue: null, OnItemsSourceChanged));

    /// <summary>
    ///     Identifies the <see cref="ColumnLevel"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty ColumnLevelProperty = DependencyProperty.Register(
        nameof(ColumnLevel),
        typeof(int),
        typeof(MenuColumnPresenter),
        new PropertyMetadata(0));

    private const string ItemsHostPart = "PART_ItemsHost";

    private StackPanel? itemsHost;
    private INotifyCollectionChanged? observableItems;
    private MenuFlyoutPresenter? ownerPresenter;
    private MenuNavigationMode? pendingFocusMode;

    /// <summary>
    ///     Initializes a new instance of the <see cref="MenuColumnPresenter"/> class.
    /// </summary>
    public MenuColumnPresenter()
    {
        this.DefaultStyleKey = typeof(MenuColumnPresenter);
        Debug.WriteLine("[MenuColumnPresenter] ctor initialized");
    }

    /// <summary>
    ///     Gets or sets the interaction controller that coordinates events for the presented items.
    /// </summary>
    public MenuInteractionController? Controller
    {
        get => (MenuInteractionController?)this.GetValue(ControllerProperty);
        set => this.SetValue(ControllerProperty, value);
    }

    /// <summary>
    ///     Gets or sets the items displayed by the presenter.
    /// </summary>
    public IEnumerable? ItemsSource
    {
        get => (IEnumerable?)this.GetValue(ItemsSourceProperty);
        set => this.SetValue(ItemsSourceProperty, value);
    }

    /// <summary>
    ///     Gets or sets the zero-based column level served by the presenter (0 == root column).
    /// </summary>
    public int ColumnLevel
    {
        get => (int)this.GetValue(ColumnLevelProperty);
        set => this.SetValue(ColumnLevelProperty, value);
    }

    /// <summary>
    ///     Gets or sets the owning <see cref="MenuFlyoutPresenter"/> responsible for column-level surface operations.
    /// </summary>
    internal MenuFlyoutPresenter? OwnerPresenter
    {
        get => this.ownerPresenter;
        set => this.ownerPresenter = value;
    }

    /// <summary>
    ///     Attempts to focus the supplied menu item within the column.
    /// </summary>
    /// <param name="menuItem">The item to focus.</param>
    /// <param name="navigationMode">The navigation mode guiding the focus action.</param>
    internal void FocusItem(MenuItemData menuItem, MenuNavigationMode navigationMode = MenuNavigationMode.PointerInput)
    {
        Debug.WriteLine($"[MenuColumnPresenter:{this.ColumnLevel}] FocusItem request for {menuItem.Id} (mode={navigationMode})");
        if (this.itemsHost is null)
        {
            Debug.WriteLine($"[MenuColumnPresenter:{this.ColumnLevel}] FocusItem aborted: itemsHost not ready");
            return;
        }

        var target = this.itemsHost.Children
            .OfType<MenuItem>()
            .FirstOrDefault(item => ReferenceEquals(item.ItemData, menuItem));

        if (target is null)
        {
            Debug.WriteLine($"[MenuColumnPresenter:{this.ColumnLevel}] FocusItem failed: target not realized");
            return;
        }

        var focusState = navigationMode == MenuNavigationMode.KeyboardInput
            ? FocusState.Keyboard
            : FocusState.Programmatic;

        target.Focus(focusState);
        //Debug.WriteLine($"[MenuColumnPresenter:{this.ColumnLevel}] Focus applied to {menuItem.Id} (state={focusState})");
    }

    /// <summary>
    ///     Focuses the first focusable item in the column.
    /// </summary>
    /// <param name="navigationMode">The navigation mode guiding the focus action.</param>
    internal void FocusFirstItem(MenuNavigationMode navigationMode)
    {
        Debug.WriteLine($"[MenuColumnPresenter:{this.ColumnLevel}] FocusFirstItem (mode={navigationMode})");
        if (this.itemsHost is null)
        {
            Debug.WriteLine($"[MenuColumnPresenter:{this.ColumnLevel}] FocusFirstItem deferred: itemsHost not ready");
            this.pendingFocusMode = navigationMode;
            return;
        }

        this.pendingFocusMode = null;

        var focusable = this.GetFocusableItems();
        if (focusable.Count == 0)
        {
            Debug.WriteLine($"[MenuColumnPresenter:{this.ColumnLevel}] FocusFirstItem aborted: no focusable items");
            return;
        }

        var focusState = navigationMode == MenuNavigationMode.KeyboardInput
            ? FocusState.Keyboard
            : FocusState.Programmatic;

        var target = focusable[0];
        _ = this.DispatcherQueue.TryEnqueue(() => target.Focus(focusState));
        //Debug.WriteLine($"[MenuColumnPresenter:{this.ColumnLevel}] FocusFirstItem scheduled on {target.ItemData?.Id} (state={focusState})");
    }

    /// <summary>
    ///     Handles key input for column-level navigation and activation.
    /// </summary>
    /// <param name="e">Key event args.</param>
    protected override void OnKeyDown(KeyRoutedEventArgs e)
    {
        if (this.HandleNavigationKey(e.Key, this.GetFocusedMenuItem()))
        {
            e.Handled = true;
            return;
        }

        base.OnKeyDown(e);
    }

    /// <inheritdoc />
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        this.itemsHost = this.GetTemplateChild(ItemsHostPart) as StackPanel
            ?? throw new InvalidOperationException($"{nameof(MenuColumnPresenter)} template must contain a StackPanel named '{ItemsHostPart}'.");

        this.RebuildItems();

        if (this.pendingFocusMode.HasValue)
        {
            var mode = this.pendingFocusMode.Value;
            this.pendingFocusMode = null;
            //Debug.WriteLine($"[MenuColumnPresenter:{this.ColumnLevel}] OnApplyTemplate applying deferred FocusFirstItem (mode={mode})");
            this.FocusFirstItem(mode);
        }
        else if (this.Controller?.NavigationMode == MenuNavigationMode.KeyboardInput)
        {
            //Debug.WriteLine($"[MenuColumnPresenter:{this.ColumnLevel}] OnApplyTemplate auto FocusFirstItem for keyboard navigation");
            this.FocusFirstItem(MenuNavigationMode.KeyboardInput);
        }
    }

    private static void OnItemsSourceChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        var presenter = (MenuColumnPresenter)d;
        presenter.HandleItemsSourceChanged(e.OldValue as IEnumerable, e.NewValue as IEnumerable);
    }

    private static void OnControllerChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        // TODO: confirm no-op and delete this method, or properly implement controller change handling
    }

    private static MenuInteractionInputSource ToActivationSource(MenuNavigationMode navigationMode) =>
        navigationMode == MenuNavigationMode.KeyboardInput
            ? MenuInteractionInputSource.KeyboardInput
            : MenuInteractionInputSource.PointerInput;

    private void HandleItemsSourceChanged(IEnumerable? oldValue, IEnumerable? newValue)
    {
        _ = oldValue; // unused
        this.DetachCollection();
        this.AttachCollection(newValue);
        this.RebuildItems();
    }

    private void AttachCollection(IEnumerable? source)
    {
        if (source is INotifyCollectionChanged observable)
        {
            this.observableItems = observable;
            this.observableItems.CollectionChanged += this.OnItemsSourceCollectionChanged;
        }
    }

    private void DetachCollection()
    {
        if (this.observableItems is not null)
        {
            this.observableItems.CollectionChanged -= this.OnItemsSourceCollectionChanged;
            this.observableItems = null;
        }
    }

    private void OnItemsSourceCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        _ = sender;
        _ = e;
        _ = this.DispatcherQueue.TryEnqueue(this.RebuildItems);
    }

    private void RebuildItems()
    {
        Debug.WriteLine($"[MenuColumnPresenter:{this.ColumnLevel}] RebuildItems start");
        if (this.itemsHost is null)
        {
            return;
        }

        this.ClearItems();

        if (this.ItemsSource is null)
        {
            return;
        }

        foreach (var itemData in this.ItemsSource.OfType<MenuItemData>())
        {
            var menuItem = new MenuItem { ItemData = itemData };
            this.HookMenuItem(menuItem);
            this.itemsHost.Children.Add(menuItem);
        }
    }

    private void ClearItems()
    {
        if (this.itemsHost is null)
        {
            return;
        }

        Debug.WriteLine($"[MenuColumnPresenter:{this.ColumnLevel}] Clearing {this.itemsHost.Children.Count} items");
        foreach (var child in this.itemsHost.Children.OfType<MenuItem>())
        {
            this.UnhookMenuItem(child);
        }

        this.itemsHost.Children.Clear();
    }

    private void HookMenuItem(MenuItem item)
    {
        item.PreviewKeyDown += this.OnMenuItemPreviewKeyDown;
        item.Invoked += this.OnMenuItemInvoked;
        item.SubmenuRequested += this.OnMenuItemSubmenuRequested;
        item.RadioGroupSelectionRequested += this.OnMenuItemRadioGroupSelectionRequested;
        item.HoverStarted += this.OnMenuItemHoverStarted;
        item.GotFocus += this.OnMenuItemGotFocus;
    }

    private void UnhookMenuItem(MenuItem item)
    {
        item.PreviewKeyDown -= this.OnMenuItemPreviewKeyDown;
        item.Invoked -= this.OnMenuItemInvoked;
        item.SubmenuRequested -= this.OnMenuItemSubmenuRequested;
        item.RadioGroupSelectionRequested -= this.OnMenuItemRadioGroupSelectionRequested;
        item.HoverStarted -= this.OnMenuItemHoverStarted;
        item.GotFocus -= this.OnMenuItemGotFocus;
    }

    private void OnMenuItemPreviewKeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (this.HandleNavigationKey(e.Key, sender as MenuItem))
        {
            e.Handled = true;
        }
    }

    private void OnMenuItemHoverStarted(object? sender, MenuItemHoverEventArgs e)
    {
        if (sender is not FrameworkElement origin)
        {
            return;
        }

        var controller = this.Controller;
        if (controller is null)
        {
            return;
        }

        var context = this.CreateCurrentContext();
        _ = origin.DispatcherQueue.TryEnqueue(() =>
        {
            if (!ReferenceEquals(this.Controller, controller))
            {
                return;
            }

            controller.OnPointerEntered(context, origin, e.ItemData);
        });
    }

    private void OnMenuItemRadioGroupSelectionRequested(object? sender, MenuItemRadioGroupEventArgs e)
    {
        this.Controller?.OnRadioGroupSelectionRequested(e.ItemData);
    }

    private void OnMenuItemInvoked(object? sender, MenuItemInvokedEventArgs e)
    {
        var controller = this.Controller;
        var navigationMode = controller?.NavigationMode ?? MenuNavigationMode.PointerInput;
        var source = ToActivationSource(navigationMode);

        var context = this.CreateCurrentContext();
        Debug.WriteLine($"[MenuColumnPresenter:{this.ColumnLevel}] Invoked {e.ItemData.Id} source={source}");
        controller?.OnInvokeRequested(context, e.ItemData, source);
    }

    private void OnMenuItemSubmenuRequested(object? sender, MenuItemSubmenuEventArgs e)
    {
        if (sender is not FrameworkElement origin)
        {
            return;
        }

        var controller = this.Controller;
        if (controller is null)
        {
            return;
        }

        var source = ToActivationSource(controller.NavigationMode);
        var context = this.CreateCurrentContext();
        Debug.WriteLine($"[MenuColumnPresenter:{this.ColumnLevel}] SubmenuRequested {e.ItemData.Id} source={source}");
        _ = origin.DispatcherQueue.TryEnqueue(() =>
        {
            if (!ReferenceEquals(this.Controller, controller))
            {
                Debug.WriteLine($"[MenuColumnPresenter:{this.ColumnLevel}] SubmenuRequested ignored: controller changed before dispatch");
                return;
            }

            controller.OnSubmenuRequested(context, origin, e.ItemData, source);
        });
    }

    private void OnMenuItemGotFocus(object sender, RoutedEventArgs e)
    {
        if (sender is not MenuItem menuItem || menuItem.ItemData is null)
        {
            return;
        }

        if (menuItem.FocusState != FocusState.Keyboard)
        {
            return;
        }

        var controller = this.Controller;
        if (controller is null)
        {
            return;
        }

        var context = this.CreateCurrentContext();
        controller.OnFocusRequested(context, menuItem, menuItem.ItemData, MenuInteractionInputSource.KeyboardInput, openSubmenu: false);
    }

    private bool OpenSubmenu(MenuItem menuItem)
    {
        Debug.Assert(menuItem.ItemData is { }, "Expecting menu item with valid data in TryOpenSubmenu");
        Debug.Assert(menuItem.ItemData.HasChildren, "Expecting menu item with children in TryOpenSubmenu");

        if (menuItem.ItemData is not MenuItemData data || !data.HasChildren)
        {
            return false;
        }

        Debug.WriteLine($"[MenuColumnPresenter:{this.ColumnLevel}] TryOpenSubmenu direct on {data.Id}");

        var controller = this.Controller;
        if (controller is null)
        {
            return false;
        }

        var context = this.CreateCurrentContext();
        Debug.WriteLine($"[MenuColumnPresenter:{this.ColumnLevel}] TryOpenSubmenuFor requesting focus for {data.Id}");
        controller.OnFocusRequested(context, menuItem, data, MenuInteractionInputSource.KeyboardInput, openSubmenu: false);

        _ = menuItem.DispatcherQueue.TryEnqueue(() =>
        {
            if (!ReferenceEquals(this.Controller, controller))
            {
                Debug.WriteLine($"[MenuColumnPresenter:{this.ColumnLevel}] TryOpenSubmenuFor dispatch skipped: controller changed before dispatch for {data.Id}");
                return;
            }

            Debug.WriteLine($"[MenuColumnPresenter:{this.ColumnLevel}] TryOpenSubmenuFor dispatching submenu request for {data.Id}");
            controller.OnSubmenuRequested(context, menuItem, data, MenuInteractionInputSource.KeyboardInput);
        });
        return true;
    }

    private MenuItem? GetFocusedMenuItem()
    {
        if (FocusManager.GetFocusedElement(this.XamlRoot) is MenuItem focused && focused.ItemData is not null)
        {
            return focused;
        }

        return this.itemsHost?.Children.OfType<MenuItem>().FirstOrDefault(item => item.FocusState == FocusState.Keyboard);
    }

    private bool TryFocusParentColumn()
    {
        if (this.ColumnLevel <= 0 || this.Parent is not Panel panel)
        {
            return false;
        }

        var index = panel.Children.IndexOf(this);
        if (index <= 0)
        {
            return false;
        }

        if (panel.Children[index - 1] is not MenuColumnPresenter parentPresenter)
        {
            return false;
        }

        var target = parentPresenter.GetActiveOrFirstFocusableItem();
        if (target?.ItemData is not MenuItemData itemData)
        {
            return false;
        }

        target.Focus(FocusState.Keyboard);
        var context = this.CreateContext(parentPresenter.ColumnLevel);
        var controller = this.Controller;
        controller?.OnFocusRequested(
            context,
            target,
            itemData,
            MenuInteractionInputSource.KeyboardInput,
            openSubmenu: false);
        Debug.WriteLine($"[MenuColumnPresenter:{this.ColumnLevel}] Focus moved to parent column {parentPresenter.ColumnLevel} item {itemData.Id}");
        return true;
    }

    private bool TryReturnFocusToRoot()
    {
        var controller = this.Controller;
        if (this.ColumnLevel != 0 || controller is null)
        {
            return false;
        }

        var owner = this.OwnerPresenter ?? throw new InvalidOperationException("Owner presenter is not set for MenuColumnPresenter.");
        var rootSurface = owner.RootSurface;
        if (rootSurface is null)
        {
            return false;
        }

        var rootItem = controller.GetActiveItem(0);
        if (rootItem is null)
        {
            return false;
        }

        var context = MenuInteractionContext.ForRoot(rootSurface, owner);
        controller.OnFocusRequested(context, origin: null, rootItem, MenuInteractionInputSource.KeyboardInput, openSubmenu: false);
        Debug.WriteLine($"[MenuColumnPresenter:{this.ColumnLevel}] Returned focus to root item {rootItem.Id}");
        return true;
    }

    private bool TryNavigateRootSibling(MenuInteractionHorizontalDirection direction)
    {
        var controller = this.Controller;
        if (controller is null)
        {
            return false;
        }

        var owner = this.OwnerPresenter ?? throw new InvalidOperationException("Owner presenter is not set for MenuColumnPresenter.");
        if (owner.RootSurface is null)
        {
            return false;
        }

        var context = MenuInteractionContext.ForColumn(this.ColumnLevel, owner, owner.RootSurface);
        controller.OnRootNavigationRequested(context, direction);
        Debug.WriteLine($"[MenuColumnPresenter:{this.ColumnLevel}] Requested root navigation {direction} from column level {this.ColumnLevel}");
        return true;
    }

    private MenuInteractionContext CreateContext(int columnLevel)
    {
        var owner = this.OwnerPresenter ?? throw new InvalidOperationException("Owner presenter is not set for MenuColumnPresenter.");
        return MenuInteractionContext.ForColumn(columnLevel, owner, owner.RootSurface);
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

    private bool TryMoveFocus(int direction)
    {
        if (direction == 0 || this.itemsHost is null)
        {
            return false;
        }

        var focusableItems = this.GetFocusableItems();
        if (focusableItems.Count == 0)
        {
            return false;
        }

        var current = this.GetFocusedMenuItem();
        var currentIndex = current is null ? -1 : focusableItems.IndexOf(current);

        if (currentIndex < 0)
        {
            var firstIndex = direction > 0 ? 0 : focusableItems.Count - 1;
            focusableItems[firstIndex].Focus(FocusState.Keyboard);
            return true;
        }

        var nextIndex = (currentIndex + direction + focusableItems.Count) % focusableItems.Count;
        focusableItems[nextIndex].Focus(FocusState.Keyboard);
        Debug.WriteLine($"[MenuColumnPresenter:{this.ColumnLevel}] TryMoveFocus moved from {currentIndex} to {nextIndex}");
        return true;
    }

    private bool HandleNavigationKey(VirtualKey key, MenuItem? origin)
    {
        var controller = this.Controller;
        if (controller is null)
        {
            return false;
        }

        controller.OnNavigationSourceChanged(MenuInteractionInputSource.KeyboardInput);

        return key switch
        {
            VirtualKey.Up => this.TryMoveFocus(-1),
            VirtualKey.Down => this.TryMoveFocus(1),
            VirtualKey.Left => this.TryFocusParentColumn() || this.TryNavigateRootSibling(MenuInteractionHorizontalDirection.Previous) || this.TryReturnFocusToRoot(),
            VirtualKey.Right => origin?.ItemData?.HasChildren == true ? this.OpenSubmenu(origin) : this.TryNavigateRootSibling(MenuInteractionHorizontalDirection.Next),
            _ => false,
        };
    }

    private List<MenuItem> GetFocusableItems()
    {
        var list = this.itemsHost?.Children
            .OfType<MenuItem>()
            .Where(item => item.ItemData is { IsEnabled: true } data && !data.IsSeparator)
            .ToList()
            ?? new List<MenuItem>();

        return list;
    }
}
