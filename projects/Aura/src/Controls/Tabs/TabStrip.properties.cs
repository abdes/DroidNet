// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using DroidNet.Aura.Drag;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura.Controls;

/// <summary>
///     A lightweight, reusable tab strip control for WinUI 3 that displays a dynamic row of tabs
///     and raises events or executes commands when tabs are invoked, selected, or closed.
/// </summary>
public partial class TabStrip
{
    /// <summary>
    ///     Identifies the <see cref="TabWidthPolicy"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty TabWidthPolicyProperty =
        DependencyProperty.Register(
            nameof(TabWidthPolicy),
            typeof(TabWidthPolicy),
            typeof(TabStrip),
            new PropertyMetadata(TabWidthPolicy.Auto, OnTabWidthPolicyPropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="MaxItemWidth"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty MaxItemWidthProperty =
        DependencyProperty.Register(
            nameof(MaxItemWidth),
            typeof(double),
            typeof(TabStrip),
            new PropertyMetadata(240.0, OnWidthPropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="PreferredItemWidth"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty PreferredItemWidthProperty =
        DependencyProperty.Register(
            nameof(PreferredItemWidth),
            typeof(double),
            typeof(TabStrip),
            new PropertyMetadata(240.0, OnWidthPropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="Items"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty ItemsProperty =
        DependencyProperty.Register(
            nameof(Items),
            typeof(ObservableCollection<TabItem>),
            typeof(TabStrip),
            new PropertyMetadata(defaultValue: null));

    /// <summary>
    ///     The collection used to populate pinned tabs (left side).
    /// </summary>
    public static readonly DependencyProperty PinnedItemsViewProperty
        = DependencyProperty.Register(
            nameof(PinnedItemsView),
            typeof(IReadOnlyList<TabItem>),
            typeof(TabStrip),
            new PropertyMetadata(defaultValue: null));

    /// <summary>
    ///     The collection used to populate regular tabs (scrollable host).
    /// </summary>
    public static readonly DependencyProperty RegularItemsViewProperty
        = DependencyProperty.Register(
            nameof(RegularItemsView),
            typeof(IReadOnlyList<TabItem>),
            typeof(TabStrip),
            new PropertyMetadata(defaultValue: null));

    /// <summary>
    ///     Identifies the <see cref="SelectedItem"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty SelectedItemProperty =
        DependencyProperty.Register(
            nameof(SelectedItem),
            typeof(TabItem),
            typeof(TabStrip),
            new PropertyMetadata(defaultValue: null, OnSelectedItemPropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="SelectedIndex"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty SelectedIndexProperty =
        DependencyProperty.Register(
            nameof(SelectedIndex),
            typeof(int),
            typeof(TabStrip),
            new PropertyMetadata(-1));

    /// <summary>
    ///     Identifies the <see cref="ScrollOnWheel"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty ScrollOnWheelProperty =
        DependencyProperty.Register(
            nameof(ScrollOnWheel),
            typeof(bool),
            typeof(TabStrip),
            new PropertyMetadata(defaultValue: true, OnScrollOnWheelPropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="LoggerFactory"/> dependency property.
    ///     Hosts can provide an <see cref="ILoggerFactory"/> to enable logging for this control.
    /// </summary>
    public static readonly DependencyProperty LoggerFactoryProperty =
        DependencyProperty.Register(
            nameof(LoggerFactory),
            typeof(ILoggerFactory),
            typeof(TabStrip),
            new PropertyMetadata(defaultValue: null, OnLoggerFactoryPropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="DragCoordinator"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty DragCoordinatorProperty =
        DependencyProperty.Register(
            nameof(DragCoordinator),
            typeof(ITabDragCoordinator),
            typeof(TabStrip),
            new PropertyMetadata(defaultValue: null, OnDragCoordinatorPropertyChanged));

    /// <summary>
    ///     Gets the collection of <see cref="TabItem"/> objects displayed in the strip.
    /// </summary>
    public ObservableCollection<TabItem> Items
        => (ObservableCollection<TabItem>)this.GetValue(ItemsProperty);

    /// <summary>
    ///     Gets the collection used to populate pinned tabs (left side).
    /// </summary>
    public IReadOnlyList<TabItem>? PinnedItemsView
        => (IReadOnlyList<TabItem>?)this.GetValue(PinnedItemsViewProperty);

    /// <summary>
    ///    Gets the collection used to populate regular tabs (scrollable host).
    /// </summary>
    public IReadOnlyList<TabItem>? RegularItemsView
        => (IReadOnlyList<TabItem>?)this.GetValue(RegularItemsViewProperty);

    /// <summary>
    ///     Gets or sets the currently selected <see cref="TabItem"/> in the strip.
    /// </summary>
    public TabItem? SelectedItem
    {
        get => (TabItem?)this.GetValue(SelectedItemProperty);
        set => this.SetValue(SelectedItemProperty, value);
    }

    /// <summary>
    ///     Gets or sets the index of the currently selected tab in the <see cref="Items"/> collection.
    ///     Returns −1 if no tab is selected.
    /// </summary>
    public int SelectedIndex
    {
        get => (int)this.GetValue(SelectedIndexProperty);
        set => this.SetValue(SelectedIndexProperty, value);
    }

    /// <summary>
    ///     Gets or sets the width policy applied to tabs in the strip.
    /// </summary>
    public TabWidthPolicy TabWidthPolicy
    {
        get => (TabWidthPolicy)this.GetValue(TabWidthPolicyProperty);
        set => this.SetValue(TabWidthPolicyProperty, value);
    }

    /// <summary>
    ///     Gets or sets the maximum width allowed for any tab item in the strip.
    /// </summary>
    public double MaxItemWidth
    {
        get => (double)this.GetValue(MaxItemWidthProperty);
        set => this.SetValue(MaxItemWidthProperty, value);
    }

    /// <summary>
    ///     Gets or sets the preferred width for any tab item in the strip.
    /// </summary>
    public double PreferredItemWidth
    {
        get => (double)this.GetValue(PreferredItemWidthProperty);
        set => this.SetValue(PreferredItemWidthProperty, Math.Min(value, this.MaxItemWidth));
    }

    /// <summary>
    ///     Gets or sets a value indicating whether the mouse wheel scrolls the tab strip
    ///     when the number of tabs exceeds the available width.
    /// </summary>
    public bool ScrollOnWheel
    {
        get => (bool)this.GetValue(ScrollOnWheelProperty);
        set => this.SetValue(ScrollOnWheelProperty, value);
    }

    /// <summary>
    ///     Gets or sets the <see cref="ILoggerFactory"/> used to create a logger for this control.
    ///     Assigning the factory initializes the internal logger to a non-null instance
    ///     (falls back to <see cref="NullLoggerFactory.Instance"/> if null).
    /// </summary>
    public ILoggerFactory? LoggerFactory
    {
        get => (ILoggerFactory?)this.GetValue(LoggerFactoryProperty);
        set => this.SetValue(LoggerFactoryProperty, value);
    }

    /// <summary>
    ///     Gets or sets the <see cref="TabDragCoordinator"/> that manages drag lifecycle for this strip.
    ///     This is typically set once during control initialization to the process-wide singleton coordinator.
    ///     Setting this property manages subscription to coordinator events automatically.
    /// </summary>
    public ITabDragCoordinator? DragCoordinator
    {
        get => (TabDragCoordinator?)this.GetValue(DragCoordinatorProperty);
        set => this.SetValue(DragCoordinatorProperty, value);
    }

    private static void OnTabWidthPolicyPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((TabStrip)d).OnTabWidthPolicyChanged((TabWidthPolicy)e.NewValue);

    private static void OnSelectedItemPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((TabStrip)d).OnSelectedItemChanged((TabItem?)e.OldValue, (TabItem?)e.NewValue);

    private static void OnScrollOnWheelPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((TabStrip)d).OnScrollOnWheelChanged((bool)e.NewValue);

    private static void OnLoggerFactoryPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((TabStrip)d).OnLoggerFactoryChanged((ILoggerFactory?)e.NewValue);

    private static void OnDragCoordinatorPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((TabStrip)d).OnDragCoordinatorChanged((ITabDragCoordinator?)e.OldValue, (ITabDragCoordinator?)e.NewValue);

    private static void OnWidthPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((TabStrip)d).RecalculateAndApplyTabWidths();

    private void OnLoggerFactoryChanged(ILoggerFactory? loggerFactory)
    {
        this.logger = loggerFactory?.CreateLogger<TabStrip>() ??
                      NullLoggerFactory.Instance.CreateLogger<TabStrip>();
        this.LayoutManager.LoggerFactory = loggerFactory;
    }
}
