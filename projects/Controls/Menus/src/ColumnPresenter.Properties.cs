// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using System.Collections.Specialized;
using Microsoft.UI.Xaml;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Presents a single vertical column of <see cref="MenuItem"/> controls.
/// </summary>
public sealed partial class ColumnPresenter
{
    /// <summary>
    ///     Identifies the <see cref="MenuSource"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty MenuSourceProperty = DependencyProperty.Register(
        nameof(MenuSource),
        typeof(IMenuSource),
        typeof(MenuFlyout),
        new PropertyMetadata(defaultValue: null));

    /// <summary>
    ///     Identifies the <see cref="Controller"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty ControllerProperty = DependencyProperty.Register(
        nameof(Controller),
        typeof(MenuInteractionController),
        typeof(ColumnPresenter),
        new PropertyMetadata(defaultValue: null, OnControllerChanged));

    /// <summary>
    ///     Identifies the <see cref="ItemsSource"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty ItemsSourceProperty = DependencyProperty.Register(
        nameof(ItemsSource),
        typeof(IEnumerable),
        typeof(ColumnPresenter),
        new PropertyMetadata(defaultValue: null, OnItemsSourceChanged));

    /// <summary>
    ///     Identifies the <see cref="ColumnLevel"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty ColumnLevelProperty = DependencyProperty.Register(
        nameof(ColumnLevel),
        typeof(int),
        typeof(ColumnPresenter),
        new PropertyMetadata(0));

    /// <summary>
    ///     Gets or sets the menu source that provides items and shared services for the bar.
    /// </summary>
    public IMenuSource? MenuSource
    {
        get => (IMenuSource?)this.GetValue(MenuSourceProperty);
        set => this.SetValue(MenuSourceProperty, value);
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

    private static void OnControllerChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        // TODO: confirm no-op and delete this method, or properly implement controller change handling
    }

    private static void OnItemsSourceChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        var presenter = (ColumnPresenter)d;
        presenter.HandleItemsSourceChanged(e.OldValue as INotifyCollectionChanged, e.NewValue as INotifyCollectionChanged);
    }

    private void HandleItemsSourceChanged(INotifyCollectionChanged? oldValue, INotifyCollectionChanged? newValue)
    {
        _ = oldValue; // unuased

        this.observableItems?.CollectionChanged -= this.OnItemsSourceCollectionChanged;
        this.observableItems = newValue;
        this.observableItems?.CollectionChanged += this.OnItemsSourceCollectionChanged;
    }

    private void OnItemsSourceCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
        => _ = this.DispatcherQueue.TryEnqueue(() =>
        {
            this.ClearItems();
            this.PopulateItems();
        });
}
