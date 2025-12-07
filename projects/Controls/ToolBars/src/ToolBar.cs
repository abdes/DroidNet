// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;
using Windows.Foundation;

namespace DroidNet.Controls;

/// <summary>
/// Represents a toolbar control that arranges primary and secondary items, manages overflow, and supports customizable layout and logging.
/// </summary>
[ContentProperty(Name = "PrimaryItems")]
[TemplatePart(Name = PrimaryItemsControlPartName, Type = typeof(ItemsControl))]
[TemplatePart(Name = OverflowButtonPartName, Type = typeof(Button))]
[TemplatePart(Name = OverflowMenuFlyoutPartName, Type = typeof(MenuFlyout))]
[TemplatePart(Name = RootGridPartName, Type = typeof(Grid))]
public partial class ToolBar : Control
{
    /// <summary>
    /// The template part name for the primary items control.
    /// </summary>
    public const string PrimaryItemsControlPartName = "PrimaryItemsControl";

    /// <summary>
    /// The template part name for the secondary items control.
    /// </summary>
    public const string SecondaryItemsControlPartName = "SecondaryItemsControl";

    /// <summary>
    /// The template part name for the overflow button.
    /// </summary>
    public const string OverflowButtonPartName = "OverflowButton";

    /// <summary>
    /// The template part name for the overflow menu flyout.
    /// </summary>
    public const string OverflowMenuFlyoutPartName = "OverflowMenuFlyout";

    /// <summary>
    /// The template part name for the root grid.
    /// </summary>
    public const string RootGridPartName = "RootGrid";

    private ILogger? logger;
    private ItemsControl? primaryItemsControl;
    private ItemsControl? secondaryItemsControl;
    private Button? overflowButton;
    private MenuFlyout? overflowMenuFlyout;
    private Grid? rootGrid;

    private double cachedSecondaryItemsWidth;

    /// <summary>
    /// Initializes a new instance of the <see cref="ToolBar"/> class.
    /// </summary>
    public ToolBar()
    {
        this.DefaultStyleKey = typeof(ToolBar);
        this.PrimaryItems = [];
        this.SecondaryItems = [];
        this.PrimaryItems.CollectionChanged += this.PrimaryItems_CollectionChanged;
        this.SecondaryItems.CollectionChanged += this.SecondaryItems_CollectionChanged;

        this.SizeChanged += this.OnSizeChanged;
    }

    /// <summary>
    /// Gets a value indicating whether the toolbar has secondary items.
    /// </summary>
    public bool HasSecondaryItems => this.SecondaryItems.Count > 0;

    /// <inheritdoc/>
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        this.primaryItemsControl = this.GetTemplateChild(PrimaryItemsControlPartName) as ItemsControl;
        this.secondaryItemsControl = this.GetTemplateChild(SecondaryItemsControlPartName) as ItemsControl;
        this.overflowButton = this.GetTemplateChild(OverflowButtonPartName) as Button;
        this.overflowMenuFlyout = this.GetTemplateChild(OverflowMenuFlyoutPartName) as MenuFlyout;
        this.rootGrid = this.GetTemplateChild(RootGridPartName) as Grid;

        if (this.primaryItemsControl != null)
        {
            this.primaryItemsControl.Loaded += (s, e) => this.UpdateOverflow();
        }

        this.UpdateOverflow();
    }

    /// <summary>
    /// Updates the overflow state and menu for the toolbar based on available space and item measurements.
    /// </summary>
    protected void UpdateOverflow()
    {
        if (this.primaryItemsControl == null
            || this.overflowButton == null
            || this.overflowMenuFlyout == null
            || this.rootGrid is not Grid { ActualWidth: > 0 })
        {
            return;
        }

        var measurements = this.MeasureToolbarComponents();
        var overflowState = this.DetermineOverflowState(measurements);

        this.UpdateSecondaryItemsVisibility(overflowState);
        this.BuildOverflowMenu(overflowState.ItemsToOverflow);
        this.OverflowButtonVisibility = overflowState.ShowOverflow ? Visibility.Visible : Visibility.Collapsed;

        this.LogOverflowUpdated(this.PrimaryItems.Count - overflowState.ItemsToOverflow.Count, overflowState.ItemsToOverflow.Count);
    }

    private static void PropagateToItems(IEnumerable<object>? items, Action<ToolBarButton> buttonAction, Action<ToolBarToggleButton> toggleAction)
    {
        if (items == null)
        {
            return;
        }

        foreach (var item in items)
        {
            if (item is ToolBarButton btn)
            {
                buttonAction(btn);
            }
            else if (item is ToolBarToggleButton tbtn)
            {
                toggleAction(tbtn);
            }
        }
    }

    // Helper to get spacing between elements (uses Margin.Right if available, otherwise defaults to 0)
    private static double GetElementSpacing(FrameworkElement element)
    {
        if (element != null)
        {
            if (element.Margin.Right > 0)
            {
                return element.Margin.Right;
            }
        }

        return 0.0;
    }

    private static MenuFlyoutItem? CreateMenuItemForCommand(object commandItem)
    {
        if (commandItem is ToolBarButton button)
        {
            var menuItem = new MenuFlyoutItem
            {
                Text = button.Label ?? string.Empty,
                Command = button.Command,
                CommandParameter = button.CommandParameter,
            };

            if (button.Icon != null)
            {
                var iconSourceElement = new IconSourceElement
                {
                    IconSource = button.Icon,
                };
                menuItem.Icon = iconSourceElement;
            }

            return menuItem;
        }

        if (commandItem is ToolBarToggleButton toggleButton)
        {
            var menuItem = new MenuFlyoutItem
            {
                Text = toggleButton.Label ?? string.Empty,
                Command = toggleButton.Command,
                CommandParameter = toggleButton.CommandParameter,
            };

            if (toggleButton.Icon != null)
            {
                var iconSourceElement = new IconSourceElement
                {
                    IconSource = toggleButton.Icon,
                };
                menuItem.Icon = iconSourceElement;
            }

            return menuItem;
        }

        return null;
    }

    private static List<object> ApplyPrimaryItemsOverflow(List<(object item, FrameworkElement container, double width, bool isSupported)> itemInfos, double availableWidth)
    {
        var totalWidth = ShowUnsupportedAndSum();
        var (overflowed, collapsedIndices, _) = FitSupportedItems(totalWidth);
        CollapseSeparators(collapsedIndices, overflowed);

        return overflowed;

        double ShowUnsupportedAndSum()
        {
            var total = 0.0;
            for (var idx = 0; idx < itemInfos.Count; idx++)
            {
                var (_, container, width, isSupported) = itemInfos[idx];
                if (!isSupported)
                {
                    container.Visibility = Visibility.Visible;
                    total += width;
                }
            }

            return total;
        }

        (List<object> overflowed, HashSet<int> collapsedIndices, double totalWidth) FitSupportedItems(double startingTotal)
        {
            var total = startingTotal;
            var overflowed = new List<object>();
            var collapsedIndices = new HashSet<int>();

            for (var i = 0; i < itemInfos.Count; i++)
            {
                if (!itemInfos[i].isSupported)
                {
                    continue;
                }

                // If this is a separator, do not collapse if it is BETWEEN two non-supported controls
                if (itemInfos[i].item is ToolBarSeparator)
                {
                    var leftIsUnsupported = i > 0 && !itemInfos[i - 1].isSupported;
                    var rightIsUnsupported = i < itemInfos.Count - 1 && !itemInfos[i + 1].isSupported;
                    if (leftIsUnsupported && rightIsUnsupported)
                    {
                        itemInfos[i].container.Visibility = Visibility.Visible;
                        continue;
                    }
                }

                if (total + itemInfos[i].width > availableWidth)
                {
                    itemInfos[i].container.Visibility = Visibility.Collapsed;
                    overflowed.Add(itemInfos[i].item);
                    collapsedIndices.Add(i);
                }
                else
                {
                    itemInfos[i].container.Visibility = Visibility.Visible;
                    total += itemInfos[i].width;
                }
            }

            return (overflowed, collapsedIndices, total);
        }

        void CollapseSeparators(HashSet<int> collapsedIndices, List<object> overflowed)
        {
            for (var i = 0; i < itemInfos.Count; i++)
            {
                if (itemInfos[i].item is ToolBarSeparator)
                {
                    var leftIsSupported = i > 0 && itemInfos[i - 1].isSupported;
                    var rightIsSupported = i < itemInfos.Count - 1 && itemInfos[i + 1].isSupported;
                    var leftIsCollapsed = i > 0 && collapsedIndices.Contains(i - 1);
                    var rightIsCollapsed = i < itemInfos.Count - 1 && collapsedIndices.Contains(i + 1);

                    // Only collapse if adjacent to a collapsed supported item and neither neighbor is unsupported
                    if ((leftIsSupported && leftIsCollapsed && (!rightIsSupported || rightIsCollapsed)) ||
                        (rightIsSupported && rightIsCollapsed && (!leftIsSupported || leftIsCollapsed)))
                    {
                        // But do not collapse if either neighbor is unsupported
                        if ((i > 0 && !itemInfos[i - 1].isSupported) || (i < itemInfos.Count - 1 && !itemInfos[i + 1].isSupported))
                        {
                            continue;
                        }

                        itemInfos[i].container.Visibility = Visibility.Collapsed;
                        overflowed.Add(itemInfos[i].item);
                    }
                }
            }
        }
    }

    private void OnSizeChanged(object sender, SizeChangedEventArgs e)
        => this.UpdateOverflow();

    private void OnIsCompactChanged()
    {
        this.LogIsCompactChanged(this.IsCompact);
        this.PropagateIsCompactToChildren(this.IsCompact);
    }

    private void OnDefaultLabelPositionChanged()
        => this.PropagateDefaultLabelPositionToChildren();

    private void OnPrimaryItemsCollectionChanged(ObservableCollection<object>? oldCollection, ObservableCollection<object>? newCollection)
    {
        oldCollection?.CollectionChanged -= this.PrimaryItems_CollectionChanged;
        newCollection?.CollectionChanged += this.PrimaryItems_CollectionChanged;

        this.UpdateOverflow();
    }

    private void OnSecondaryItemsCollectionChanged(ObservableCollection<object>? oldCollection, ObservableCollection<object>? newCollection)
    {
        oldCollection?.CollectionChanged -= this.SecondaryItems_CollectionChanged;
        newCollection?.CollectionChanged += this.SecondaryItems_CollectionChanged;

        this.cachedSecondaryItemsWidth = 0;
        this.UpdateOverflow();
    }

    private void PropagateIsCompactToChildren(bool isCompact)
    {
        PropagateToItems(
            this.PrimaryItems,
            btn => btn.IsCompact = isCompact,
            tbtn => tbtn.IsCompact = isCompact);
        PropagateToItems(
            this.SecondaryItems,
            btn => btn.IsCompact = isCompact,
            tbtn => tbtn.IsCompact = isCompact);
    }

    private void PropagateDefaultLabelPositionToChildren()
    {
        PropagateToItems(
            this.PrimaryItems,
            btn => btn.UpdateLabelPosition(),
            tbtn => tbtn.UpdateLabelPosition());
        PropagateToItems(
            this.SecondaryItems,
            btn => btn.UpdateLabelPosition(),
            tbtn => tbtn.UpdateLabelPosition());
    }

    private void PropagateLoggerFactoryToChildren(ILoggerFactory? factory)
    {
        PropagateToItems(
            this.PrimaryItems,
            btn => btn.LoggerFactory = factory,
            tbtn => tbtn.LoggerFactory = factory);
        PropagateToItems(
            this.SecondaryItems,
            btn => btn.LoggerFactory = factory,
            tbtn => tbtn.LoggerFactory = factory);
    }

    private void PrimaryItems_CollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        this.LogPrimaryItemsChanged(e.Action.ToString());

        if (e.NewItems == null)
        {
            return;
        }

        foreach (var item in e.NewItems)
        {
            if (item is ToolBarButton btn)
            {
                btn.IsCompact = this.IsCompact;
                btn.LoggerFactory = this.GetValue(LoggerFactoryProperty) as ILoggerFactory;
                btn.UpdateLabelPosition();
            }
            else if (item is ToolBarToggleButton tbtn)
            {
                tbtn.IsCompact = this.IsCompact;
                tbtn.LoggerFactory = this.GetValue(LoggerFactoryProperty) as ILoggerFactory;
                tbtn.UpdateLabelPosition();
            }
        }

        this.UpdateOverflow();
    }

    private void SecondaryItems_CollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        this.LogSecondaryItemsChanged(e.Action.ToString());

        // Reset cache when items are added/removed
        this.cachedSecondaryItemsWidth = 0;

        if (e.NewItems == null)
        {
            return;
        }

        foreach (var item in e.NewItems)
        {
            if (item is ToolBarButton btn)
            {
                btn.IsCompact = this.IsCompact;
                btn.LoggerFactory = this.GetValue(LoggerFactoryProperty) as ILoggerFactory;
                btn.UpdateLabelPosition();
            }
            else if (item is ToolBarToggleButton tbtn)
            {
                tbtn.IsCompact = this.IsCompact;
                tbtn.LoggerFactory = this.GetValue(LoggerFactoryProperty) as ILoggerFactory;
                tbtn.UpdateLabelPosition();
            }
        }

        this.UpdateOverflow();
    }

    private ToolbarMeasurements MeasureToolbarComponents()
    {
        Debug.Assert(this.rootGrid != null, "root grid should not be null when measuring toolbar components.");
        Debug.Assert(this.overflowButton != null, "overflow button should not be null when measuring toolbar components.");

        // Overlay button: always subtract its width from available space
        var totalWidth = this.rootGrid.ActualWidth;

        // Start with an empty measurements record and let helper methods populate it.
        var measurements = new ToolbarMeasurements(
            Total: totalWidth,
            Secondary: 0,
            SecondaryIsCached: false,
            OverflowButton: 0,
            PrimaryItemInfos: [],
            PrimaryTotal: 0);

        // Populate secondary measurements into the record
        measurements = this.MeasureSecondaryItems(measurements);

        // Measure overflow button
        this.overflowButton.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
        var overflowWidth = this.overflowButton.DesiredSize.Width + GetElementSpacing(this.overflowButton);
        measurements = measurements with { OverflowButton = overflowWidth };

        // Populate primary item infos and primary total
        measurements = this.MeasurePrimaryItemsInfo(measurements);

        return measurements;
    }

    private ToolbarMeasurements MeasureSecondaryItems(ToolbarMeasurements measurements)
    {
        if (!this.HasSecondaryItems || this.secondaryItemsControl == null)
        {
            return measurements with { Secondary = 0, SecondaryIsCached = false };
        }

        // We only measure the actual width of secondary items if they are visible;
        // Otherwise, we will use the cached width from the last measurement.
        if (this.secondaryItemsControl.Visibility == Visibility.Visible)
        {
            var width = 0.0;
            for (var i = 0; i < this.SecondaryItems!.Count; i++)
            {
                if (this.secondaryItemsControl.ContainerFromIndex(i) is FrameworkElement container)
                {
                    container.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
                    width += container.DesiredSize.Width + GetElementSpacing(container);
                }
            }

            this.cachedSecondaryItemsWidth = width;
            return measurements with { Secondary = width, SecondaryIsCached = false };
        }

        return measurements with { Secondary = this.cachedSecondaryItemsWidth, SecondaryIsCached = true };
    }

    private ToolbarMeasurements MeasurePrimaryItemsInfo(ToolbarMeasurements measurements)
    {
        var primaryInfos = new List<(object item, FrameworkElement container, double width, bool isSupported)>();
        for (var i = 0; i < this.PrimaryItems.Count; i++)
        {
            var item = this.PrimaryItems[i];
            if (this.primaryItemsControl!.ContainerFromIndex(i) is not FrameworkElement container)
            {
                continue;
            }

            var isSupported = item is ToolBarButton or ToolBarToggleButton or ToolBarSeparator;

            // Temporarily make collapsed containers visible for measurement, then restore.
            var wasCollapsed = container.Visibility == Visibility.Collapsed;
            if (wasCollapsed)
            {
                container.Visibility = Visibility.Visible;
            }

            container.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
            var itemWidth = container.DesiredSize.Width + GetElementSpacing(container);

            if (wasCollapsed)
            {
                container.Visibility = Visibility.Collapsed;
            }

            primaryInfos.Add((item, container, itemWidth, isSupported));
        }

        var primaryTotal = 0.0;
        for (var i = 0; i < primaryInfos.Count; i++)
        {
            primaryTotal += primaryInfos[i].width;
        }

        return measurements with { PrimaryItemInfos = primaryInfos, PrimaryTotal = primaryTotal };
    }

    private OverflowState DetermineOverflowState(ToolbarMeasurements measurements)
    {
        Debug.Assert(this.rootGrid != null, "root grid should not be null when determining overflow state.");

        // Get padding from rootGrid if available
        var pad = this.rootGrid.Padding;
        var padding = pad.Left + pad.Right;

        // Overlay button: always subtract its width from total available width
        var availableWithSecondaries = measurements.Total - measurements.Secondary - measurements.OverflowButton - padding;
        var hasOverflowWithSecondaries = measurements.PrimaryTotal > availableWithSecondaries;

        if (!hasOverflowWithSecondaries)
        {
            // Everything fits
            SetAllPrimaryItemsVisible();
            return new OverflowState(this.HasSecondaryItems, ShowOverflow: false, []);
        }

        // Phase 2: Try without secondaries
        var availableWithoutSecondaries = measurements.Total - measurements.OverflowButton - padding;
        var hasOverflowWithoutSecondaries = measurements.PrimaryTotal > availableWithoutSecondaries;

        if (!hasOverflowWithoutSecondaries)
        {
            SetAllPrimaryItemsVisible();
            return new OverflowState(ShowSecondaries: false, ShowOverflow: true, []);
        }

        var overflowed = ApplyPrimaryItemsOverflow(measurements.PrimaryItemInfos, availableWithoutSecondaries);
        return new OverflowState(ShowSecondaries: false, ShowOverflow: true, overflowed);

        void SetAllPrimaryItemsVisible()
        {
            for (var i = 0; i < this.PrimaryItems.Count; i++)
            {
                var container = this.primaryItemsControl!.ContainerFromIndex(i) as FrameworkElement;
                _ = container?.Visibility = Visibility.Visible;
            }
        }
    }

    private void UpdateSecondaryItemsVisibility(OverflowState state)
        => this.secondaryItemsControl?.Visibility = state.ShowSecondaries
            ? Visibility.Visible
            : Visibility.Collapsed;

    private void BuildOverflowMenu(List<object> overflowedPrimaries)
    {
        this.overflowMenuFlyout!.Items.Clear();

        AddItemsToMenu(overflowedPrimaries);

        if (this.HasSecondaryItems)
        {
            if (overflowedPrimaries.Count > 0)
            {
                this.overflowMenuFlyout.Items.Add(new MenuFlyoutSeparator());
            }

            AddItemsToMenu([.. this.SecondaryItems!]);
        }

        void AddItemsToMenu(List<object> items)
        {
            object? lastAdded = null;
            foreach (var item in items)
            {
                if (item is ToolBarButton or ToolBarToggleButton)
                {
                    if (CreateMenuItemForCommand(item) is MenuFlyoutItem menuItem)
                    {
                        this.overflowMenuFlyout!.Items.Add(menuItem);
                        lastAdded = item;
                    }
                }
                else if (item is ToolBarSeparator)
                {
                    // Avoid consecutive separators
                    if (lastAdded is ToolBarSeparator)
                    {
                        continue;
                    }

                    this.overflowMenuFlyout!.Items.Add(new MenuFlyoutSeparator());
                    lastAdded = item;
                }
                else
                {
                    // Unsupported item type; skip
                }
            }
        }
    }

    private record ToolbarMeasurements(
        double Total,
        double Secondary,
        bool SecondaryIsCached,
        double OverflowButton,
        List<(object item, FrameworkElement container, double width, bool isSupported)> PrimaryItemInfos,
        double PrimaryTotal);

    private record OverflowState(bool ShowSecondaries, bool ShowOverflow, List<object> ItemsToOverflow);
}
