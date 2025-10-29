// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Collections;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Controls;

/// <summary>
///     A lightweight, reusable tab strip control for WinUI 3 that displays a dynamic row of tabs
///     and raises events or executes commands when tabs are invoked, selected, or closed.
/// </summary>
[TemplatePart(Name = RootGridPartName, Type = typeof(Grid))]
[TemplatePart(Name = PartOverflowLeftButtonName, Type = typeof(RepeatButton))]
[TemplatePart(Name = PartOverflowRightButtonName, Type = typeof(RepeatButton))]
[TemplatePart(Name = PartPinnedItemsRepeaterName, Type = typeof(ItemsRepeater))]
[TemplatePart(Name = PartRegularItemsRepeaterName, Type = typeof(ItemsRepeater))]
[TemplatePart(Name = PartScrollHostName, Type = typeof(ScrollViewer))]
[SuppressMessage("Design", "CA1001:Types that own disposable fields should be disposable", Justification = "Dispatcher-backed proxy fields are disposed in the Unloaded handler to align with control lifetime.")]
public partial class TabStrip : Control
{
    private const string RootGridPartName = "PartRootGrid";
    private const string PartOverflowLeftButtonName = "PartOverflowLeftButton";
    private const string PartOverflowRightButtonName = "PartOverflowRightButton";
    private const string PartPinnedItemsRepeaterName = "PartPinnedItemsRepeater";
    private const string PartRegularItemsRepeaterName = "PartRegularItemsRepeater";
    private const string PartScrollHostName = "PartScrollHost";

    private const double ScrollEpsilon = 1.0;

    private readonly DispatcherCollectionProxy<TabItem>? pinnedProxy;
    private readonly DispatcherCollectionProxy<TabItem>? regularProxy;
    private bool proxiesDisposed;

    // Guard to coalesce compact sizing enqueues per-spec (prevent layout thrash)
    private bool compactSizingEnqueued;

    // Suppress handling when we programmatically move/modify the Items collection to avoid reentrancy
    private bool suppressCollectionChangeHandling;

    private Grid? rootGrid;
    private RepeatButton? overflowLeftButton;
    private RepeatButton? overflowRightButton;
    private ItemsRepeater? pinnedItemsRepeater;
    private ItemsRepeater? regularItemsRepeater;
    private ScrollViewer? scrollHost;

    private ILogger? logger;

    /// <summary>
    ///     Initializes a new instance of the <see cref="TabStrip" /> class.
    /// </summary>
    public TabStrip()
    {
        this.DefaultStyleKey = typeof(TabStrip);

        // Ensure Items exists (Items never changes for this control instance)
        var items = new ObservableCollection<TabItem>();

        // Log when items are added to help diagnose runtime additions
        items.CollectionChanged += this.Items_CollectionChanged;
        this.SetValue(ItemsProperty, items);

        // Create filtered views over the Items source. Only IsPinned matters for re-evaluation.
        var pinnedView = new FilteredObservableCollection<TabItem>(items, ti => ti.IsPinned, [nameof(TabItem.IsPinned)]);
        var regularView = new FilteredObservableCollection<TabItem>(items, ti => !ti.IsPinned, [nameof(TabItem.IsPinned)]);

        // Use a weak reference to the control for the enqueue delegate so we don't capture
        // the control instance strongly (which could cause a lifetime leak via the
        // DispatcherCollectionProxy holding the delegate).
        var weakSelf = new WeakReference<TabStrip>(this);
        bool Enqueue(Action a)
        {
            if (weakSelf.TryGetTarget(out var target))
            {
                return target.DispatcherQueue.TryEnqueue(() => a());
            }

            // Control has been collected/unavailable; indicate we couldn't enqueue.
            return false;
        }

        // Dispose proxies when the control is unloaded to break event handler chains
        // and allow sources and proxies to be reclaimed.
        this.Unloaded += this.TabStrip_Unloaded;

        // Create dispatcher-backed read-only proxies so we continue exposing ReadOnlyObservableCollection<TabItem>
        // (this also guarantees collection-changed handling occurs on the control's dispatcher).
        this.pinnedProxy = new DispatcherCollectionProxy<TabItem>(pinnedView, pinnedView, Enqueue);
        this.regularProxy = new DispatcherCollectionProxy<TabItem>(regularView, regularView, Enqueue);

        this.SetValue(PinnedItemsViewProperty, this.pinnedProxy);
        this.SetValue(RegularItemsViewProperty, this.regularProxy);
    }

    // Filtered views and dispatcher-backed proxies created in constructor. The FilteredObservableCollection
    // instances keep themselves in sync with the Items source; the DispatcherReadOnlyObservableCollection
    // forwards changes to the UI dispatcher and exposes a ReadOnlyObservableCollection<TabItem> surface.

    /// <inheritdoc />
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        void DetachPreviousHandlers()
        {
            if (this.overflowLeftButton is not null)
            {
                this.overflowLeftButton.Click -= this.OverflowLeftButton_Click;
            }

            if (this.overflowRightButton is not null)
            {
                this.overflowRightButton.Click -= this.OverflowRightButton_Click;
            }

            if (this.pinnedItemsRepeater is not null)
            {
                this.pinnedItemsRepeater.ElementPrepared -= this.ItemsRepeater_ElementPrepared;
                this.pinnedItemsRepeater.ElementClearing -= this.ItemsRepeater_ElementClearing;
            }

            if (this.regularItemsRepeater is not null)
            {
                this.regularItemsRepeater.ElementPrepared -= this.ItemsRepeater_ElementPrepared;
                this.regularItemsRepeater.ElementClearing -= this.ItemsRepeater_ElementClearing;
            }

            if (this.scrollHost is not null)
            {
                this.scrollHost.ViewChanged -= this.ScrollHost_ViewChanged;
            }

            this.PointerWheelChanged -= this.ScrollHost_PointerWheelChanged;
        }

        void RetrieveTemplateParts()
        {
            T? TryGetPart<T>(string name)
                where T : DependencyObject
            {
                var part = this.GetTemplateChild(name) as T;
                if (part is null)
                {
                    this.LogTemplatePartNotFound(name);
                }

                return part;
            }

            this.rootGrid = TryGetPart<Grid>(RootGridPartName);
            this.overflowLeftButton = TryGetPart<RepeatButton>(PartOverflowLeftButtonName);
            this.overflowRightButton = TryGetPart<RepeatButton>(PartOverflowRightButtonName);
            this.pinnedItemsRepeater = TryGetPart<ItemsRepeater>(PartPinnedItemsRepeaterName);
            this.regularItemsRepeater = TryGetPart<ItemsRepeater>(PartRegularItemsRepeaterName);
            this.scrollHost = TryGetPart<ScrollViewer>(PartScrollHostName);
        }

        void WireHandlers()
        {
            if (this.overflowLeftButton is not null)
            {
                this.overflowLeftButton.Click += this.OverflowLeftButton_Click;
            }

            if (this.overflowRightButton is not null)
            {
                this.overflowRightButton.Click += this.OverflowRightButton_Click;
            }

            if (this.pinnedItemsRepeater is not null)
            {
                this.pinnedItemsRepeater.ElementPrepared += this.ItemsRepeater_ElementPrepared;
                this.pinnedItemsRepeater.ElementClearing += this.ItemsRepeater_ElementClearing;
                this.pinnedItemsRepeater.ItemsSource = this.PinnedItemsView;
            }

            if (this.regularItemsRepeater is not null)
            {
                this.regularItemsRepeater.ElementPrepared += this.ItemsRepeater_ElementPrepared;
                this.regularItemsRepeater.ElementClearing += this.ItemsRepeater_ElementClearing;
                this.regularItemsRepeater.ItemsSource = this.RegularItemsView;
            }

            this.OnScrollOnWheelChanged(this.ScrollOnWheel);
        }

        void SetupSizeChangedHandlers()
        {
            if (this.scrollHost is not null)
            {
                this.scrollHost.SizeChanged -= this.OnTabStripSizeChanged;
                this.scrollHost.SizeChanged += this.OnTabStripSizeChanged;
                this.scrollHost.ViewChanged -= this.ScrollHost_ViewChanged;
                this.scrollHost.ViewChanged += this.ScrollHost_ViewChanged;
            }

            if (this.rootGrid is not null)
            {
                this.rootGrid.SizeChanged -= this.OnTabStripSizeChanged;
                this.rootGrid.SizeChanged += this.OnTabStripSizeChanged;
            }
        }

        DetachPreviousHandlers();
        RetrieveTemplateParts();
        WireHandlers();
        SetupSizeChangedHandlers();
        this.UpdateOverflowButtonVisibility();

        // Ensure initial compact sizing run after template applied (deferred/coalesced)
        if (this.TabWidthPolicy == TabWidthPolicy.Compact)
        {
            this.EnqueueCompactSizing();
        }
    }

    private void ApplyTabSizingPolicy(FrameworkElement fe, ItemsRepeater sender)
    {
        if (fe is not TabStripItem tsi)
        {
            return;
        }

        var maxWidth = this.MaxItemWidth;
        var preferredWidth = this.PreferredItemWidth;
        var minWidth = tsi.MinWidth;

        // Clamp minWidth to maxWidth as per specs
        minWidth = Math.Min(minWidth, maxWidth);

        // Enforce the spec: the effective MinWidth for the item must be clamped to MaxItemWidth
        if (tsi.MinWidth != minWidth)
        {
            tsi.MinWidth = minWidth;
        }

        int tabCount;
        double availableWidth;
        var isPinned = sender == this.pinnedItemsRepeater;

        if (isPinned)
        {
            tabCount = this.PinnedItemsView?.Count ?? 0;

            // Prefer the pinned repeater's available width for per-repeater Equal calculations.
            availableWidth = this.pinnedItemsRepeater?.ActualWidth ?? this.rootGrid?.ActualWidth ?? 0;
        }
        else
        {
            tabCount = this.RegularItemsView?.Count ?? 0;
            availableWidth = this.scrollHost?.ActualWidth ?? 0;
        }

        Debug.WriteLine($"ApplyTabSizingPolicy: Policy={this.TabWidthPolicy}, IsPinned={isPinned}, TabCount={tabCount}, AvailableWidth={availableWidth}, MaxWidth={maxWidth}, PreferredWidth={preferredWidth}");

        double? calculatedWidth = this.TabWidthPolicy switch
        {
            TabWidthPolicy.Equal => preferredWidth,
            TabWidthPolicy.Compact => null, // Auto
            _ => null,
        };

        if (calculatedWidth.HasValue)
        {
            fe.Width = Math.Max(minWidth, Math.Min(maxWidth, calculatedWidth.Value));
            fe.MaxWidth = maxWidth;
            Debug.WriteLine($"ApplyTabSizingPolicy: Set Width={fe.Width}, MaxWidth={fe.MaxWidth}");
        }
        else
        {
            fe.ClearValue(FrameworkElement.WidthProperty);
            fe.MaxWidth = maxWidth;
            Debug.WriteLine($"ApplyTabSizingPolicy: Cleared Width, Set MaxWidth={fe.MaxWidth}");
        }

        // Log sizing for troubleshooting
        this.LogTabSizing(this.TabWidthPolicy, calculatedWidth, maxWidth, tabCount, availableWidth);
    }

    private void ItemsRepeater_ElementPrepared(ItemsRepeater sender, ItemsRepeaterElementPreparedEventArgs args)
    {
        // Wire up Tapped event for selection logic and apply sizing policy.
        if (args.Element is FrameworkElement fe && fe.DataContext is TabItem ti)
        {
            fe.SetValue(AutomationProperties.NameProperty, ti.Header);
            ti.IsSelected = this.SelectedItem == ti;

            // Remove previous handler to avoid duplicate subscriptions
            fe.Tapped -= this.OnTabElementTapped;
            fe.Tapped += this.OnTabElementTapped;

            this.ApplyTabSizingPolicy(fe, sender);
        }

        // Set IsCompact on TabStripItem based on TabWidthPolicy and repeater
        if (args.Element is TabStripItem tsi)
        {
            var isCompact = (this.TabWidthPolicy == TabWidthPolicy.Compact) && (sender == this.regularItemsRepeater);
            tsi.IsCompact = isCompact;
            Debug.WriteLine($"ItemsRepeater_ElementPrepared: Set IsCompact={isCompact} for {(sender == this.regularItemsRepeater ? "regular" : "pinned")} item");

            // Ensure TabStripItem.MinWidth is clamped to TabStrip.MaxItemWidth per spec.
            var effectiveMin = Math.Min(tsi.MinWidth, this.MaxItemWidth);
            if (tsi.MinWidth != effectiveMin)
            {
                tsi.MinWidth = effectiveMin;
            }

            // Register for close requested event
            tsi.CloseRequested += this.OnTabCloseRequested;

            // If we're in Compact policy and this is a regular item, ensure we run the
            // compact-sizing pass after the container is prepared so newly-prepared
            // items are included in the calculation.
            if (isCompact)
            {
                this.EnqueueCompactSizing();
            }
        }
    }

    private void ItemsRepeater_ElementClearing(ItemsRepeater sender, ItemsRepeaterElementClearingEventArgs args)
    {
        // Unwire Tapped event to prevent memory leaks.
        if (args.Element is FrameworkElement fe)
        {
            fe.Tapped -= this.OnTabElementTapped;
        }

        // Unwire CloseRequested event
        if (args.Element is TabStripItem tsi)
        {
            tsi.CloseRequested -= this.OnTabCloseRequested;
        }
    }

    private void OnTabElementTapped(object? sender, TappedRoutedEventArgs e)
    {
        if (sender is FrameworkElement fe && fe.DataContext is TabItem ti)
        {
            // Invoke command if present
            ti.Command?.Execute(ti.CommandParameter);

            // Set SelectedItem from the full Items collection
            var index = -1;
            if (this.Items is ObservableCollection<TabItem> items)
            {
                index = items.IndexOf(ti);
                if (index >= 0)
                {
                    this.SelectedIndex = index;
                    this.SelectedItem = ti;
                }
            }

            // Raise TabInvoked event
            this.TabInvoked?.Invoke(this, new TabInvokedEventArgs { Item = ti, Index = index, Parameter = ti.CommandParameter });
            this.LogTabInvoked(ti, index);
        }
    }

    private void OnSelectedItemChanged(TabItem? oldItem, TabItem? newItem)
    {
        // Update IsSelected flags on all items from the full Items collection
        if (this.Items is ObservableCollection<TabItem> items)
        {
            var oldIndex = -1;
            var newIndex = -1;
            if (oldItem is not null)
            {
                oldIndex = items.IndexOf(oldItem);
            }

            if (newItem is not null)
            {
                newIndex = items.IndexOf(newItem);
            }

            foreach (var t in items)
            {
                t.IsSelected = ReferenceEquals(t, newItem);
            }

            this.SelectedIndex = newIndex;
            this.SelectionChanged?.Invoke(this, new TabSelectionChangedEventArgs { OldItem = oldItem, NewItem = newItem, OldIndex = oldIndex, NewIndex = newIndex });
            this.LogSelectionChanged(oldItem, newItem, oldIndex, newIndex);
        }
    }

    private void OnTabCloseRequested(object? sender, TabCloseRequestedEventArgs e)
    {
        this.TabCloseRequested?.Invoke(this, e);
    }

    private void OverflowLeftButton_Click(object? sender, RoutedEventArgs e)
    {
        if (this.scrollHost is null)
        {
            return;
        }

        var target = Math.Max(0, (int)(this.scrollHost.HorizontalOffset - 100));
        _ = this.scrollHost.ChangeView(target, verticalOffset: null, zoomFactor: null);
        this.LogOverflowLeftClicked();
    }

    private void OverflowRightButton_Click(object? sender, RoutedEventArgs e)
    {
        if (this.scrollHost is null)
        {
            return;
        }

        var target = (int)(this.scrollHost.HorizontalOffset + 100);
        _ = this.scrollHost.ChangeView(target, verticalOffset: null, zoomFactor: null);
        this.LogOverflowRightClicked();
    }

    private void UpdateOverflowButtonVisibility()
    {
        if (this.scrollHost is null)
        {
            return;
        }

        var canScrollLeft = this.scrollHost.HorizontalOffset > ScrollEpsilon;
        var canScrollRight = this.scrollHost.HorizontalOffset < this.scrollHost.ScrollableWidth - ScrollEpsilon;

        _ = this.overflowLeftButton?.Visibility = canScrollLeft ? Visibility.Visible : Visibility.Collapsed;
        _ = this.overflowRightButton?.Visibility = canScrollRight ? Visibility.Visible : Visibility.Collapsed;
    }

    private void CheckAndApplyCompactSizing()
    {
        Debug.WriteLine("CheckAndApplyCompactSizing: Starting");

        if (this.scrollHost is null || this.regularItemsRepeater is null)
        {
            Debug.WriteLine("CheckAndApplyCompactSizing: Early exit - scrollHost or regularItemsRepeater null");
            return;
        }

        // Ensure the repeater's layout is updated
        this.regularItemsRepeater.UpdateLayout();

        Debug.WriteLine($"CheckAndApplyCompactSizing: regularItemsRepeater children count = {VisualTreeHelper.GetChildrenCount(this.regularItemsRepeater)}");

        var items = new List<TabStripItem>();

        // Prefer enumerating prepared elements by index via TryGetElement so we get the container for each logical item.
        var regularCount = this.RegularItemsView?.Count ?? 0;
        for (var i = 0; i < regularCount; i++)
        {
            var element = this.regularItemsRepeater.TryGetElement(i) as TabStripItem;
            if (element is not null)
            {
                items.Add(element);
            }
        }

        // Fallback: if TryGetElement returned none OR only a subset of logical
        // items (common when some containers are not prepared/virtualized), also
        // enumerate the repeater's visual children and include any TabStripItem
        // containers we didn't already collect. This ensures prepared containers
        // are included in the compact-sizing pass even when TryGetElement is
        // partially supported or only returns some indices.
        if (items.Count == 0 || items.Count < regularCount)
        {
            for (var i = 0; i < VisualTreeHelper.GetChildrenCount(this.regularItemsRepeater); i++)
            {
                var child = VisualTreeHelper.GetChild(this.regularItemsRepeater, i);
                Debug.WriteLine($"CheckAndApplyCompactSizing: child {i} type = {child.GetType().Name}");
                if (child is TabStripItem tsi && !items.Contains(tsi))
                {
                    items.Add(tsi);
                }
            }
        }

        Debug.WriteLine($"CheckAndApplyCompactSizing: collected {items.Count} TabStripItem");

        if (items.Count == 0)
        {
            Debug.WriteLine("CheckAndApplyCompactSizing: no items");
            return;
        }

        // Step 1: Compute desiredWidths, minWidths, availableWidth, maxWidth
        var desiredWidths = new List<double>();
        var minWidths = new List<double>();
        var availableWidth = this.scrollHost.ActualWidth;
        var maxWidth = this.MaxItemWidth;

        foreach (var item in items)
        {
            // Ensure the item's template parts are applied and measured so that
            // TabStripItem.UpdateMinWidth (which relies on template children)
            // has had a chance to run. This avoids a race where TabStrip captures
            // a smaller min and the item later increases its MinWidth, causing
            // a visible jump.
            try
            {
                _ = item.ApplyTemplate();
                item.UpdateLayout();
            }
            catch
            {
                // Ignore failures - we'll still attempt to measure what is available.
            }

            // Clear any explicit Width so we can read the intrinsic desired size (Auto measure)
            item.ClearValue(FrameworkElement.WidthProperty);

            // Temporarily relax MaxWidth to allow an unconstrained measure so we capture the
            // intrinsic single-line desired width (per spec). Restore MaxWidth afterwards.
            double desired;
            var origMax = item.MaxWidth;
            try
            {
                item.MaxWidth = double.PositiveInfinity;

                // Measure with infinite available size to get intrinsic width
                item.Measure(new global::Windows.Foundation.Size(double.PositiveInfinity, double.PositiveInfinity));
                desired = item.DesiredSize.Width;
            }
            finally
            {
                // Restore original MaxWidth
                item.MaxWidth = origMax;
            }

            // Ensure we use a MinWidth clamped to MaxItemWidth per spec
            var effectiveMin = Math.Min(item.MinWidth, maxWidth);
            var clampedDesired = Math.Min(maxWidth, Math.Max(effectiveMin, desired));
            desiredWidths.Add(clampedDesired);
            minWidths.Add(effectiveMin);
            Debug.WriteLine($"CheckAndApplyCompactSizing: Item desired={desired}, clamped={clampedDesired}, min={effectiveMin}");
        }

        // Step 2: Compute sumDesired
        var sumDesired = desiredWidths.Sum();
        Debug.WriteLine($"CheckAndApplyCompactSizing: SumDesired={sumDesired}");

        if (sumDesired <= availableWidth)
        {
            // Use desired sizes
            Debug.WriteLine("CheckAndApplyCompactSizing: Using desired sizes");
            for (var i = 0; i < items.Count; i++)
            {
                items[i].MinWidth = minWidths[i];
                items[i].Width = desiredWidths[i];
                items[i].MaxWidth = maxWidth;
                Debug.WriteLine($"CheckAndApplyCompactSizing: Set item {i} Width={desiredWidths[i]}");
            }

            return;
        }

        // Step 3: Iterative shrinking
        Debug.WriteLine("CheckAndApplyCompactSizing: Starting iterative shrinking");
        var currentWidths = new List<double>(desiredWidths);
        var remainingIndices = new List<int>();
        for (var i = 0; i < items.Count; i++)
        {
            remainingIndices.Add(i);
        }

        var deficit = sumDesired - availableWidth;
        Debug.WriteLine($"CheckAndApplyCompactSizing: Initial deficit={deficit}");

        while (deficit > 0 && remainingIndices.Count > 0)
        {
            // Step 4: Compute shrinkPerItem
            var shrinkPerItem = deficit / remainingIndices.Count;
            Debug.WriteLine($"CheckAndApplyCompactSizing: ShrinkPerItem={shrinkPerItem}, Remaining={remainingIndices.Count}");

            // Attempt to shrink
            var newRemaining = new List<int>();

            foreach (var idx in remainingIndices)
            {
                var newWidth = currentWidths[idx] - shrinkPerItem;
                var clampedWidth = Math.Max(minWidths[idx], newWidth);
                Debug.WriteLine($"CheckAndApplyCompactSizing: Item {idx}: current={currentWidths[idx]}, new={newWidth}, clamped={clampedWidth}");
                currentWidths[idx] = clampedWidth;

                if (clampedWidth > minWidths[idx])
                {
                    newRemaining.Add(idx);
                }
                else
                {
                    // Clamped to min
                }
            }

            // Recompute sum and deficit
            var newSum = currentWidths.Sum();
            var newDeficit = Math.Max(0, newSum - availableWidth);
            Debug.WriteLine($"CheckAndApplyCompactSizing: NewSum={newSum}, NewDeficit={newDeficit}");

            remainingIndices = newRemaining;
            deficit = newDeficit;
        }

        // Set the final widths
        Debug.WriteLine("CheckAndApplyCompactSizing: Setting final widths");
        for (var i = 0; i < items.Count; i++)
        {
            items[i].MinWidth = minWidths[i];
            items[i].Width = currentWidths[i];
            items[i].MaxWidth = maxWidth;
            Debug.WriteLine($"CheckAndApplyCompactSizing: Final item {i} Width={currentWidths[i]}");
        }

        // Scrolling will be enabled automatically if needed
    }

    // Coalesced enqueue for compact sizing per-spec to avoid layout thrash.
    private void EnqueueCompactSizing()
    {
        if (this.compactSizingEnqueued)
        {
            return;
        }

        this.compactSizingEnqueued = true;

        // Run compact sizing at a lower dispatcher priority so the item's template
        // parts (button widths, etc.) have a chance to measure and stabilize
        // before we capture intrinsic widths and enforce Min/Max. This reduces
        // jumpy UI where the item later updates its own MinWidth.
        _ = this.DispatcherQueue.TryEnqueue(Microsoft.UI.Dispatching.DispatcherQueuePriority.Low, () =>
            {
                try
                {
                    this.CheckAndApplyCompactSizing();
                }
                finally
                {
                    this.compactSizingEnqueued = false;
                }
            });
    }

    private void ScrollHost_ViewChanged(object? sender, ScrollViewerViewChangedEventArgs e)
    {
        this.UpdateOverflowButtonVisibility();
    }

    private void ScrollHost_PointerWheelChanged(object? sender, PointerRoutedEventArgs e)
    {
        if (this.scrollHost is null)
        {
            return;
        }

        var delta = e.GetCurrentPoint(this).Properties.MouseWheelDelta;
        var scrollAmount = delta > 0 ? -50 : 50; // negative delta is up/left
        var target = Math.Max(0, Math.Min(this.scrollHost.ScrollableWidth, this.scrollHost.HorizontalOffset + scrollAmount));
        _ = this.scrollHost.ChangeView(target, verticalOffset: null, zoomFactor: null);
        e.Handled = true;
    }

    private void TabStrip_Unloaded(object? sender, RoutedEventArgs e)
    {
        // Unsubscribe immediately and dispose proxies so we do not hold onto
        // event handlers or dispatcher closures longer than necessary.
        this.Unloaded -= this.TabStrip_Unloaded;

        if (this.proxiesDisposed)
        {
            return;
        }

        this.pinnedProxy?.Dispose();
        this.regularProxy?.Dispose();
        this.proxiesDisposed = true;

        // Log unloading for troubleshooting
        this.LogUnloaded();
    }

    private void UpdateTabWidths()
    {
        void UpdateRepeater(ItemsRepeater? repeater)
        {
            if (repeater == null)
            {
                return;
            }

            // Prefer enumerating prepared container elements via TryGetElement(index) when available.
            var sourceCount = (repeater.ItemsSource as System.Collections.ICollection)?.Count ?? -1;
            if (sourceCount > 0)
            {
                for (var i = 0; i < sourceCount; i++)
                {
                    var tsi = repeater.TryGetElement(i) as TabStripItem;
                    if (tsi is not null)
                    {
                        // Apply sizing policy to the prepared element
                        this.ApplyTabSizingPolicy(tsi, repeater);
                        tsi.IsCompact = (this.TabWidthPolicy == TabWidthPolicy.Compact) && (repeater == this.regularItemsRepeater);
                    }
                }
            }
            else
            {
                // Fallback: enumerate visual children (older platform or no ItemsSource count available)
                var count = VisualTreeHelper.GetChildrenCount(repeater);
                for (var i = 0; i < count; i++)
                {
                    var child = VisualTreeHelper.GetChild(repeater, i);
                    if (child is FrameworkElement fe && fe.DataContext is TabItem)
                    {
                        this.ApplyTabSizingPolicy(fe, repeater);
                    }

                    // Update IsCompact on TabStripItem
                    if (child is TabStripItem tsi)
                    {
                        tsi.IsCompact = (this.TabWidthPolicy == TabWidthPolicy.Compact) && (repeater == this.regularItemsRepeater);
                    }
                }
            }
        }

        UpdateRepeater(this.pinnedItemsRepeater);
        UpdateRepeater(this.regularItemsRepeater);
    }

    // Called from the TabWidthPolicy dependency property change callback. We defer the
    // actual update to the dispatcher so prepared container elements (ItemsRepeater
    // containers) are available when we enumerate them in UpdateTabWidths.
    private void OnTabWidthPolicyChanged(TabWidthPolicy newPolicy)
    {
        // Defer so visual tree and repeater preparations occur first.
        _ = this.DispatcherQueue.TryEnqueue(() =>
        {
            try
            {
                this.UpdateTabWidths();

                // If switching into Compact, ensure we run the compact sizing pass too.
                if (newPolicy == TabWidthPolicy.Compact)
                {
                    this.EnqueueCompactSizing();
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"OnTabWidthPolicyChanged: deferred update failed: {ex}");
            }
        });
    }

    private void Items_CollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        var items = this.Items as ObservableCollection<TabItem>;

        // If items were added, ensure they are inserted after the current selection
        // and become the selected item. Use Move to avoid duplication of elements.
        if (!this.suppressCollectionChangeHandling && e.Action == NotifyCollectionChangedAction.Add && e.NewItems is not null && items is not null)
        {
            // We cannot modify the ObservableCollection synchronously inside its CollectionChanged
            // event (it throws). Defer moves/selection changes to after the current dispatch via the
            // DispatcherQueue. Each new item will be moved (if necessary) to follow the current
            // selection and then become the selected item.
            foreach (var newItem in e.NewItems)
            {
                if (newItem is TabItem ti)
                {
                    // Log addition now (collection already contains the new item)
                    var count = items.Count;
                    this.LogItemAdded(ti, count);

                    // Compute desired index based on current selection snapshot
                    int desiredIndex;
                    if (items.Count == 1)
                    {
                        desiredIndex = 0;
                    }
                    else if (this.SelectedItem is TabItem sel && items.Contains(sel))
                    {
                        var selIndex = items.IndexOf(sel);
                        desiredIndex = Math.Min(selIndex + 1, items.Count - 1);
                    }
                    else
                    {
                        desiredIndex = items.Count - 1;
                    }

                    // Defer the Move and selection to after the CollectionChanged event completes.
                    _ = this.DispatcherQueue.TryEnqueue(() =>
                    {
                        if (this.suppressCollectionChangeHandling)
                        {
                            return;
                        }

                        try
                        {
                            this.suppressCollectionChangeHandling = true;

                            if (items.Contains(ti))
                            {
                                var curIndex = items.IndexOf(ti);
                                var target = Math.Min(Math.Max(0, desiredIndex), items.Count - 1);
                                if (curIndex >= 0 && curIndex != target)
                                {
                                    items.Move(curIndex, target);
                                }

                                // Newly added item becomes the selected item
                                this.SelectedItem = ti;
                            }
                        }
                        finally
                        {
                            this.suppressCollectionChangeHandling = false;
                        }

                        // Update layout/sizing after move/selection
                        this.pinnedItemsRepeater?.InvalidateMeasure();
                        this.regularItemsRepeater?.InvalidateMeasure();
                        this.UpdateLayout();
                        if (this.TabWidthPolicy == TabWidthPolicy.Compact)
                        {
                            this.EnqueueCompactSizing();
                        }

                        this.DispatcherQueue.TryEnqueue(() => this.UpdateOverflowButtonVisibility());
                    });
                }
            }
        }
        else
        {
            // If we didn't handle logging above, still log additions for diagnostics
            if (e.Action == NotifyCollectionChangedAction.Add && e.NewItems is not null)
            {
                foreach (var newItem in e.NewItems)
                {
                    if (newItem is TabItem ti)
                    {
                        var count = items is not null ? items.Count : -1;
                        this.LogItemAdded(ti, count);
                    }
                }
            }
        }

        // Re-apply widths when tab collection changes
        this.pinnedItemsRepeater?.InvalidateMeasure();
        this.regularItemsRepeater?.InvalidateMeasure();

        // Force layout to update DesiredSize
        this.UpdateLayout();

        if (this.TabWidthPolicy == TabWidthPolicy.Compact)
        {
            this.EnqueueCompactSizing();
        }

        // If items were removed and the previously selected item is no longer present,
        // choose the previous item if available, otherwise the next item. If the
        // collection is empty, leave SelectedItem null.
        if (!this.suppressCollectionChangeHandling && e.Action == NotifyCollectionChangedAction.Remove && e.OldItems is not null && items is not null)
        {
            // If the selected item was removed, select a neighbor
            if (this.SelectedItem is TabItem sel && !items.Contains(sel))
            {
                if (items.Count == 0)
                {
                    this.SelectedItem = null;
                    this.SelectedIndex = -1;
                }
                else
                {
                    var removedIndex = e.OldStartingIndex;
                    var candidateIndex = removedIndex - 1;
                    if (candidateIndex >= 0 && candidateIndex < items.Count)
                    {
                        this.SelectedItem = items[candidateIndex];
                    }
                    else
                    {
                        // pick the item at the removedIndex (which is now the next item), or the last item
                        var fallback = Math.Min(Math.Max(0, removedIndex), items.Count - 1);
                        this.SelectedItem = items[fallback];
                    }
                }
            }
        }

        // Ensure there is always a selected item if the collection is not empty
        if (!this.suppressCollectionChangeHandling && items?.Count > 0 && this.SelectedItem is null)
        {
            this.SelectedItem = items[0];
        }

        this.DispatcherQueue.TryEnqueue(() => this.UpdateOverflowButtonVisibility());
    }

    private void OnTabStripSizeChanged(object? sender, SizeChangedEventArgs e)
    {
        Debug.WriteLine($"OnTabStripSizeChanged: NewSize={e.NewSize}, Policy={this.TabWidthPolicy}");

        // Force ItemsRepeater to re-prepare items so widths are recalculated
        this.pinnedItemsRepeater?.InvalidateMeasure();
        this.regularItemsRepeater?.InvalidateMeasure();

        // Force layout to update DesiredSize
        this.UpdateLayout();

        this.UpdateOverflowButtonVisibility();

        if (this.TabWidthPolicy == TabWidthPolicy.Compact)
        {
            this.EnqueueCompactSizing();
        }

        // Log size change for troubleshooting
        this.LogSizeChanged(e.NewSize.Width, e.NewSize.Height);
    }

    private T? FindDescendantByName<T>(DependencyObject? root, string name)
        where T : DependencyObject
    {
        if (root is null)
        {
            return null;
        }

        var childCount = VisualTreeHelper.GetChildrenCount(root);
        for (var i = 0; i < childCount; i++)
        {
            var child = VisualTreeHelper.GetChild(root, i);

            if (child is FrameworkElement fe && string.Equals(fe.Name, name, StringComparison.Ordinal) && child is T t)
            {
                return t;
            }

            var found = this.FindDescendantByName<T>(child, name);
            if (found is not null)
            {
                return found;
            }
        }

        return null;
    }
}
