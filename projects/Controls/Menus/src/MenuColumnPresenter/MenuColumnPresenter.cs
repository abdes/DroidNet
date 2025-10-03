// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections;
using System.Collections.Specialized;
using System.Linq;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls;

/// <summary>
///     Presents a single vertical column of <see cref="MenuItem"/> controls. The presenter bridges the
///     templated visual tree with <see cref="MenuInteractionController"/> so parent controls can keep their
///     logic agnostic of individual item wiring.
/// </summary>
[TemplatePart(Name = ItemsHostPart, Type = typeof(StackPanel))]
public sealed class MenuColumnPresenter : Control
{
    /// <summary>
    ///     Identifies the <see cref="Controller"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty ControllerProperty = DependencyProperty.Register(
        nameof(Controller),
        typeof(MenuInteractionController),
        typeof(MenuColumnPresenter),
        new PropertyMetadata(null, OnControllerChanged));

    /// <summary>
    ///     Identifies the <see cref="ItemsSource"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty ItemsSourceProperty = DependencyProperty.Register(
        nameof(ItemsSource),
        typeof(IEnumerable),
        typeof(MenuColumnPresenter),
        new PropertyMetadata(null, OnItemsSourceChanged));

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

    /// <summary>
    ///     Initializes a new instance of the <see cref="MenuColumnPresenter"/> class.
    /// </summary>
    public MenuColumnPresenter()
    {
        this.DefaultStyleKey = typeof(MenuColumnPresenter);
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

    /// <inheritdoc />
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        this.itemsHost = this.GetTemplateChild(ItemsHostPart) as StackPanel
            ?? throw new InvalidOperationException($"{nameof(MenuColumnPresenter)} template must contain a StackPanel named '{ItemsHostPart}'.");

        this.RebuildItems();
    }

    private static void OnItemsSourceChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        var presenter = (MenuColumnPresenter)d;
        presenter.HandleItemsSourceChanged(e.OldValue as IEnumerable, e.NewValue as IEnumerable);
    }

    private static void OnControllerChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        var presenter = (MenuColumnPresenter)d;

        if (e.OldValue is MenuInteractionController oldController)
        {
            oldController.ItemInvoked -= presenter.OnControllerItemInvoked;
        }

        if (e.NewValue is MenuInteractionController newController)
        {
            newController.ItemInvoked += presenter.OnControllerItemInvoked;
        }
    }

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

        foreach (var child in this.itemsHost.Children.OfType<MenuItem>())
        {
            this.UnhookMenuItem(child);
        }

        this.itemsHost.Children.Clear();
    }

    private void HookMenuItem(MenuItem item)
    {
        item.Invoked += this.OnMenuItemInvoked;
        item.SubmenuRequested += this.OnMenuItemSubmenuRequested;
        item.RadioGroupSelectionRequested += this.OnMenuItemRadioGroupSelectionRequested;
        item.HoverEntered += this.OnMenuItemHoverEntered;
    }

    private void UnhookMenuItem(MenuItem item)
    {
        item.Invoked -= this.OnMenuItemInvoked;
        item.SubmenuRequested -= this.OnMenuItemSubmenuRequested;
        item.RadioGroupSelectionRequested -= this.OnMenuItemRadioGroupSelectionRequested;
        item.HoverEntered -= this.OnMenuItemHoverEntered;
    }

    private void OnMenuItemHoverEntered(object? sender, MenuItemHoverEventArgs e)
    {
        if (this.Controller == null)
        {
            return;
        }

        this.Controller.NotifyPointerNavigation();

        if (sender is FrameworkElement origin && e.MenuItem.HasChildren)
        {
            _ = origin.DispatcherQueue.TryEnqueue(() =>
                this.Controller?.RequestSubmenu(origin, e.MenuItem, this.ColumnLevel, MenuNavigationMode.PointerInput));
        }
    }

    private void OnMenuItemRadioGroupSelectionRequested(object? sender, MenuItemRadioGroupEventArgs e)
    {
        this.Controller?.HandleRadioGroupSelection(e.MenuItem);
    }

    private void OnMenuItemInvoked(object? sender, MenuItemInvokedEventArgs e)
    {
        this.Controller?.HandleItemInvoked(e.MenuItem);
    }

    private void OnMenuItemSubmenuRequested(object? sender, MenuItemSubmenuEventArgs e)
    {
        if (this.Controller == null || sender is not FrameworkElement origin)
        {
            return;
        }

        var mode = this.Controller.NavigationMode == MenuNavigationMode.KeyboardInput
            ? MenuNavigationMode.KeyboardInput
            : MenuNavigationMode.PointerInput;

        _ = origin.DispatcherQueue.TryEnqueue(() =>
            this.Controller?.RequestSubmenu(origin, e.MenuItem, this.ColumnLevel, mode));
    }

    private void OnControllerItemInvoked(object? sender, MenuItemInvokedEventArgs e)
    {
        // The presenter does not react directly to invoked events, but we keep the subscription so the
        // controller can manage lifetime symmetry when swapped.
        _ = sender;
        _ = e;
    }
}
