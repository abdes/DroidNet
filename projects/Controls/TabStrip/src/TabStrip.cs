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

    // Suppress handling when we programmatically move/modify the Items collection to avoid reentrancy
    private bool suppressCollectionChangeHandling;

    private Grid? rootGrid;
    private RepeatButton? overflowLeftButton;
    private RepeatButton? overflowRightButton;
    private ItemsRepeater? pinnedItemsRepeater;
    private ItemsRepeater? regularItemsRepeater;
    private ScrollViewer? scrollHost;

    private ILogger? logger;

    private ITabStripLayoutManager LayoutManager { get; } = new TabStripLayoutManager();
    // Cache of the last computed layout result to allow applying authoritative
    // widths to containers as they are prepared (prevents visual flicker).
    private LayoutResult? _lastLayoutResult;

    /// <summary>
    ///     Initializes a new instance of the <see cref="TabStrip" /> class.
    /// </summary>
    public TabStrip()
    {
        this.DefaultStyleKey = typeof(TabStrip);

        // Setup the Items collection, which is created internally and exposed
        // as a read-only property. The `Items` collection object never changes
        // for this control; only its contents change.
        var items = new ObservableCollection<TabItem>();
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

        // Create dispatcher-backed read-only proxies so we continue exposing ReadOnlyObservableCollection<TabItem>
        // (this also guarantees collection-changed handling occurs on the control's dispatcher).
        this.pinnedProxy = new DispatcherCollectionProxy<TabItem>(pinnedView, pinnedView, Enqueue);
        this.regularProxy = new DispatcherCollectionProxy<TabItem>(regularView, regularView, Enqueue);

        this.SetValue(PinnedItemsViewProperty, this.pinnedProxy);
        this.SetValue(RegularItemsViewProperty, this.regularProxy);

        // Initialize layout manager with current DP values
        this.LayoutManager.MaxItemWidth = this.MaxItemWidth;
        this.LayoutManager.PreferredItemWidth = this.PreferredItemWidth;
        this.LayoutManager.Policy = this.TabWidthPolicy;

        // Handle Unloaded to dispose of disposable fields
        this.Unloaded += this.TabStrip_Unloaded;
    }

    /// <inheritdoc />
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        this.LogApplyingTemplate();

        T? GetTemplatePart<T>(string name, bool isRequired = false)
            where T : DependencyObject
        {
            var part = this.GetTemplateChild(name) as T;
            if (part is null)
            {
                var expectedType = typeof(T);
                this.LogTemplatePartNotFound(name, expectedType, isRequired);
                if (isRequired)
                {
                    throw new InvalidOperationException($"The required template part '{name}' is missing or is not of type '{expectedType}'.");
                }
            }

            return part;
        }

        T GetRequiredTemplatePart<T>(string name)
            where T : DependencyObject
            => GetTemplatePart<T>(name, isRequired: true)!;

        void SetupRootGrid()
        {
            if (this.rootGrid is not null)
            {
                this.rootGrid.SizeChanged -= this.OnTabStripSizeChanged;
            }

            this.rootGrid = GetRequiredTemplatePart<Grid>(RootGridPartName);
            this.rootGrid.SizeChanged += this.OnTabStripSizeChanged;
        }

        void SetupOverflowLeftButton()
        {
            if (this.overflowLeftButton is not null)
            {
                this.overflowLeftButton.Click -= this.OverflowLeftButton_Click;
            }

            this.overflowLeftButton = GetTemplatePart<RepeatButton>(PartOverflowLeftButtonName);
            this.overflowLeftButton?.Click += this.OverflowLeftButton_Click;
        }

        void SetupOverflowRightButton()
        {
            if (this.overflowRightButton is not null)
            {
                this.overflowRightButton.Click -= this.OverflowRightButton_Click;
            }

            this.overflowRightButton = GetTemplatePart<RepeatButton>(PartOverflowRightButtonName);
            this.overflowRightButton?.Click += this.OverflowRightButton_Click;
        }

        void SetupPinnedItemsRepeater()
        {
            if (this.pinnedItemsRepeater is not null)
            {
                this.pinnedItemsRepeater.ElementPrepared -= this.ItemsRepeater_ElementPrepared;
                this.pinnedItemsRepeater.ElementClearing -= this.ItemsRepeater_ElementClearing;
            }

            this.pinnedItemsRepeater = GetRequiredTemplatePart<ItemsRepeater>(PartPinnedItemsRepeaterName);
            this.pinnedItemsRepeater.ElementPrepared += this.ItemsRepeater_ElementPrepared;
            this.pinnedItemsRepeater.ElementClearing += this.ItemsRepeater_ElementClearing;
            this.pinnedItemsRepeater.ItemsSource = this.PinnedItemsView;
        }

        void SetupRegularItemsRepeater()
        {
            if (this.regularItemsRepeater is not null)
            {
                this.regularItemsRepeater.ElementPrepared -= this.ItemsRepeater_ElementPrepared;
                this.regularItemsRepeater.ElementClearing -= this.ItemsRepeater_ElementClearing;
            }

            this.regularItemsRepeater = GetRequiredTemplatePart<ItemsRepeater>(PartRegularItemsRepeaterName);
            this.regularItemsRepeater.ElementPrepared += this.ItemsRepeater_ElementPrepared;
            this.regularItemsRepeater.ElementClearing += this.ItemsRepeater_ElementClearing;
            this.regularItemsRepeater.ItemsSource = this.RegularItemsView;
        }

        void SetupScrollHost()
        {
            if (this.scrollHost is not null)
            {
                this.scrollHost.ViewChanged -= this.ScrollHost_ViewChanged;
                this.scrollHost.SizeChanged -= this.OnTabStripSizeChanged;
            }

            this.scrollHost = GetRequiredTemplatePart<ScrollViewer>(PartScrollHostName);
            this.scrollHost.ViewChanged += this.ScrollHost_ViewChanged;
            this.scrollHost.SizeChanged += this.OnTabStripSizeChanged;
        }

        // Setup each part
        SetupRootGrid();
        SetupOverflowLeftButton();
        SetupOverflowRightButton();
        SetupPinnedItemsRepeater();
        SetupRegularItemsRepeater();
        SetupScrollHost();

        // Apply behaviors based on current property values
        this.OnScrollOnWheelChanged(this.ScrollOnWheel);
        this.UpdateOverflowButtonVisibility();
    }

    /// <summary>
    ///     Handles the Tapped event for a tab element. Executes the associated command, selects the
    ///     tab, and raises the <see cref="TabInvoked"/> event.
    /// </summary>
    /// <param name="sender">
    ///     The source of the event, expected to be a <see cref="FrameworkElement"/> with a
    ///     <see cref="TabItem"/> as its DataContext.
    /// </param>
    /// <param name="e">The event data for the tap event.</param>
    protected virtual void OnTabElementTapped(object? sender, TappedRoutedEventArgs e)
    {
        if (sender is FrameworkElement fe && fe.DataContext is TabItem ti)
        {
            // Invoke command if present
            ti.Command?.Execute(ti.CommandParameter);

            // Select the tab (centralized logic)
            this.SelectedItem = ti;  // This triggers OnSelectedItemChanged

            // Raise TabInvoked event
            this.TabInvoked?.Invoke(this, new TabInvokedEventArgs { Item = ti, Index = this.SelectedIndex, Parameter = ti.CommandParameter });
            this.LogTabInvoked(ti);
        }
    }

    private void ItemsRepeater_ElementPrepared(ItemsRepeater sender, ItemsRepeaterElementPreparedEventArgs args)
    {
        Debug.Assert(args.Element is FrameworkElement { DataContext: TabItem }, "control expects DataContext to be set with a TabIte, on each of its TabStrip items");

        if (args.Element is not TabStripItem { DataContext: TabItem ti } tsi)
        {
            this.LogBadItem(sender, args.Element);
            return;
        }

        var pinned = sender == this.pinnedItemsRepeater;

        this.LogSetupPreparedItem(ti, this.TabWidthPolicy == TabWidthPolicy.Compact && !pinned, pinned);

        tsi.SetValue(AutomationProperties.NameProperty, ti.Header);
        ti.IsSelected = this.SelectedItem == ti;
        tsi.LoggerFactory = this.LoggerFactory;
        tsi.IsCompact = this.TabWidthPolicy == TabWidthPolicy.Compact && !pinned;

        // Ensure TabStripItem.MinWidth is clamped to TabStrip.MaxItemWidth per spec.
        var effectiveMin = Math.Min(tsi.MinWidth, this.MaxItemWidth);
        if (tsi.MinWidth != effectiveMin)
        {
            tsi.MinWidth = effectiveMin;
        }

        tsi.Tapped -= this.OnTabElementTapped; // for safety
        tsi.Tapped += this.OnTabElementTapped;
        tsi.CloseRequested -= this.OnTabCloseRequested; // for safety
        tsi.CloseRequested += this.OnTabCloseRequested;
        // Do NOT set Width/MaxWidth here. Wait until ApplyLayoutResults after all measurements are done.

        // Recompute and apply tab widths after an element is prepared. This covers
        // the case where an item was just pinned/unpinned and moved between the
        // repeaters. Defer to the DispatcherQueue to avoid running during
        // repeater preparation (prevents reentrancy).
        _ = this.DispatcherQueue.TryEnqueue(() => this.UpdateTabWidths());
    }

    private void ItemsRepeater_ElementClearing(ItemsRepeater sender, ItemsRepeaterElementClearingEventArgs args)
    {
        Debug.Assert(args.Element is TabStripItem, "control expects each of its TabStrip items to be a TabStripItem");

        if (args.Element is not TabStripItem tsi)
        {
            this.LogBadItem(sender, args.Element);
            return;
        }

        this.LogClearElement(tsi);

        tsi.Tapped -= this.OnTabElementTapped;
        tsi.CloseRequested -= this.OnTabCloseRequested;
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

    private void OnTabCloseRequested(object? sender, TabCloseRequestedEventArgs e)
        => this.TabCloseRequested?.Invoke(this, e);

    private void OnScrollOnWheelChanged(bool newValue)
    {
        this.PointerWheelChanged -= this.ScrollHost_PointerWheelChanged;
        if (newValue)
        {
            this.PointerWheelChanged += this.ScrollHost_PointerWheelChanged;
        }
    }

    private void OnSelectedItemChanged(TabItem? oldItem, TabItem? newItem)
    {
        // If the selected item hasn't changed, do nothing
        if (ReferenceEquals(oldItem, newItem))
        {
            return;
        }

        var items = this.Items;

        // Update IsSelected flags: deselect old item and select new item
        _ = oldItem?.IsSelected = false;
        _ = newItem?.IsSelected = true;

        var oldIndex = oldItem is not null ? items.IndexOf(oldItem) : -1;
        var newIndex = newItem is not null ? items.IndexOf(newItem) : -1;

        this.SelectedIndex = newIndex;

        this.LogSelectionChanged(oldItem, newItem, oldIndex, newIndex);
        this.SelectionChanged?.Invoke(this, new TabSelectionChangedEventArgs
        {
            OldItem = oldItem,
            NewItem = newItem,
            OldIndex = oldIndex,
            NewIndex = newIndex,
        });
    }

    // Called from the TabWidthPolicy dependency property change callback.
    private void OnTabWidthPolicyChanged(TabWidthPolicy newPolicy)
    {
        Debug.WriteLine($"OnTabWidthPolicyChanged: Policy changed to {newPolicy}");

        // Sync to layout manager
        this.LayoutManager.Policy = newPolicy;

        // Update IsCompact on prepared items first, so MinWidth is updated before layout
        var containers = this.CollectPreparedContainers();
        Debug.WriteLine($"OnTabWidthPolicyChanged: Updating IsCompact on {containers.Count} prepared containers");
        foreach (var kvp in containers)
        {
            var tsi = kvp.Value;
            var ti = (TabItem)tsi.DataContext;
            var pinned = ti.IsPinned;
            var newIsCompact = newPolicy == TabWidthPolicy.Compact && !pinned;
            Debug.WriteLine($"OnTabWidthPolicyChanged: Setting IsCompact={newIsCompact} on {ti.Header} (pinned={pinned})");
            tsi.IsCompact = newIsCompact;
        }

        Debug.WriteLine("OnTabWidthPolicyChanged: Calling UpdateTabWidths");
        this.UpdateTabWidths();
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

    private void UpdateTabWidths()
    {
        Debug.WriteLine("UpdateTabWidths: Starting");

        // Sync width properties to layout manager
        this.LayoutManager.MaxItemWidth = this.MaxItemWidth;
        this.LayoutManager.PreferredItemWidth = this.PreferredItemWidth;

        var containers = this.CollectPreparedContainers();
        Debug.WriteLine($"UpdateTabWidths: Collected {containers.Count} prepared containers");
        if (containers.Count == 0)
        {
            Debug.WriteLine("UpdateTabWidths: No containers, exiting");
            return;
        }

        var desiredWidths = this.MeasureDesiredWidthsIfNeeded(containers);
        var inputs = this.BuildLayoutInputs(containers, desiredWidths);
        var result = this.ComputeLayout(inputs);
        // Cache the computed layout so newly prepared containers can apply the
        // authoritative width immediately and avoid visual flicker.
        this._lastLayoutResult = result;
        this.ApplyLayoutResults(result, containers);

        Debug.WriteLine("UpdateTabWidths: Completed");
    }

    private Dictionary<int, TabStripItem> CollectPreparedContainers()
    {
        Debug.WriteLine("CollectPreparedContainers: Starting");

        var indexToContainer = new Dictionary<int, TabStripItem>();

        void CollectFromRepeater(ItemsRepeater? repeater, bool isPinned)
        {
            if (repeater == null)
            {
                return;
            }

            var sourceCount = (repeater.ItemsSource as System.Collections.ICollection)?.Count ?? -1;
            if (sourceCount > 0)
            {
                for (var i = 0; i < sourceCount; i++)
                {
                    var tsi = repeater.TryGetElement(i) as TabStripItem;
                    if (tsi is not null && tsi.DataContext is TabItem ti)
                    {
                        var index = this.Items.IndexOf(ti);
                        indexToContainer[index] = tsi;
                    }
                }
            }
            else
            {
                // Fallback
                var count = VisualTreeHelper.GetChildrenCount(repeater);
                for (var i = 0; i < count; i++)
                {
                    var child = VisualTreeHelper.GetChild(repeater, i);
                    if (child is TabStripItem tsi && tsi.DataContext is TabItem ti)
                    {
                        var index = this.Items.IndexOf(ti);
                        indexToContainer[index] = tsi;
                    }
                }
            }
        }

        CollectFromRepeater(this.pinnedItemsRepeater, true);
        CollectFromRepeater(this.regularItemsRepeater, false);

        Debug.WriteLine($"CollectPreparedContainers: Returning {indexToContainer.Count} containers");

        return indexToContainer;
    }

    private Dictionary<TabStripItem, double> MeasureDesiredWidthsIfNeeded(Dictionary<int, TabStripItem> containers)
    {
        Debug.WriteLine("MeasureDesiredWidthsIfNeeded: Starting");

        var desiredWidths = new Dictionary<TabStripItem, double>();
        if (this.TabWidthPolicy != TabWidthPolicy.Compact)
        {
            Debug.WriteLine("MeasureDesiredWidthsIfNeeded: Policy not Compact, returning empty");
            return desiredWidths;
        }


        foreach (var tsi in containers.Values)
        {
            // Save and clear width constraints to measure natural content width
            var origWidth = tsi.ReadLocalValue(FrameworkElement.WidthProperty);
            var origMax = tsi.ReadLocalValue(FrameworkElement.MaxWidthProperty);
            var origMin = tsi.ReadLocalValue(FrameworkElement.MinWidthProperty);
            tsi.ClearValue(FrameworkElement.WidthProperty);
            tsi.ClearValue(FrameworkElement.MaxWidthProperty);
            tsi.ClearValue(FrameworkElement.MinWidthProperty);

            tsi.Measure(new global::Windows.Foundation.Size(double.PositiveInfinity, double.PositiveInfinity));
            var desired = tsi.DesiredSize.Width;
            var effectiveMin = Math.Min(tsi.MinWidth, this.MaxItemWidth);
            Debug.WriteLine($"[MeasureDesiredWidthsIfNeeded] Tab '{((tsi.DataContext as TabItem)?.Header ?? "<null>")}', measured desired={desired}, effectiveMin={effectiveMin}, result={Math.Min(this.MaxItemWidth, Math.Max(effectiveMin, desired))}");
            desiredWidths[tsi] = Math.Min(this.MaxItemWidth, Math.Max(effectiveMin, desired));

            // Restore original values
            if (origWidth != DependencyProperty.UnsetValue) tsi.SetValue(FrameworkElement.WidthProperty, origWidth);
            if (origMax != DependencyProperty.UnsetValue) tsi.SetValue(FrameworkElement.MaxWidthProperty, origMax);
            if (origMin != DependencyProperty.UnsetValue) tsi.SetValue(FrameworkElement.MinWidthProperty, origMin);
        }

        Debug.WriteLine($"MeasureDesiredWidthsIfNeeded: Measured {desiredWidths.Count} desired widths");

        return desiredWidths;
    }

    private List<LayoutPerItemInput> BuildLayoutInputs(Dictionary<int, TabStripItem> containers, Dictionary<TabStripItem, double> desiredWidths)
    {
        Debug.WriteLine("BuildLayoutInputs: Starting");

        var inputs = new List<LayoutPerItemInput>();
        foreach (var kvp in containers)
        {
            var index = kvp.Key;
            var tsi = kvp.Value;
            var ti = (TabItem)tsi.DataContext;
            var isPinned = ti.IsPinned;
            var effectiveMin = Math.Min(tsi.MinWidth, this.MaxItemWidth);
            var desired = desiredWidths.TryGetValue(tsi, out var d) ? d : effectiveMin;
            inputs.Add(new LayoutPerItemInput(index, isPinned, effectiveMin, desired));
        }

        Debug.WriteLine($"BuildLayoutInputs: Built {inputs.Count} inputs");

        return inputs;
    }

    private LayoutResult ComputeLayout(List<LayoutPerItemInput> inputs)
    {
        Debug.WriteLine("ComputeLayout: Starting");

        // Calculate precise available width for tabs inside the ScrollViewer
        double availableWidth = this.scrollHost?.ActualWidth ?? 0;
        // The ItemsRepeater for regular tabs is the only child of the ScrollViewer
        // The StackLayout for ItemsRepeater may have a variable Spacing; read it at runtime
        // We do NOT subtract overflow button widths, as they are overlaid
        // We do NOT subtract vertical scrollbar width, as vertical scroll is disabled
        // Add a small fudge factor to avoid rounding errors
        const double fudge = 1.0;

        // Always try to subtract tab spacing if we have a StackLayout and more than one tab
        StackLayout? stackLayout = this.regularItemsRepeater?.Layout as StackLayout;
        if (stackLayout != null && inputs.Count > 1)
        {
            int count = inputs.Count;
            double spacing = stackLayout.Spacing * (count - 1);
            availableWidth -= spacing;
            Debug.WriteLine($"ComputeLayout: Subtracting tab spacing: {spacing} (Spacing={stackLayout.Spacing}) for {count} tabs");
        }

        availableWidth = Math.Max(0, availableWidth - fudge);
        Debug.WriteLine($"ComputeLayout: Final available width {availableWidth}, inputs count {inputs.Count}");

        var request = new LayoutRequest(availableWidth, inputs);
        var result = this.LayoutManager.ComputeLayout(request);

        Debug.WriteLine($"ComputeLayout: Result has {result.Items.Count} items");

        return result;
    }

    private void ApplyLayoutResults(LayoutResult result, Dictionary<int, TabStripItem> containers)
    {
        Debug.WriteLine($"ApplyLayoutResults: Applying {result.Items.Count} results");

        foreach (var output in result.Items)
        {
            if (!containers.TryGetValue(output.Index, out var container))
            {
                Debug.WriteLine($"[ApplyLayoutResults] No container for index {output.Index}");
                continue;
            }

            var ti = container.DataContext as TabItem;
            var header = ti?.Header ?? $"<idx={output.Index}>";
            var prevWidth = container.Width;
            var prevMax = container.MaxWidth;
            var prevMin = container.MinWidth;

            container.MinWidth = Math.Min(container.MinWidth, this.MaxItemWidth);

            if (this.TabWidthPolicy == TabWidthPolicy.Equal)
            {
                container.Width = output.Width;
                container.MaxWidth = this.MaxItemWidth;
                Debug.WriteLine($"[ApplyLayoutResults] (Equal) Tab '{header}' idx={output.Index}: Width set to {output.Width}, MaxWidth={this.MaxItemWidth}, MinWidth={container.MinWidth}");
            }
            else if (this.TabWidthPolicy == TabWidthPolicy.Compact)
            {
                container.Width = output.Width;
                container.MaxWidth = this.MaxItemWidth;
                Debug.WriteLine($"[ApplyLayoutResults] (Compact) Tab '{header}' idx={output.Index}: Width set to {output.Width}, MaxWidth={this.MaxItemWidth}, MinWidth={container.MinWidth}");
            }
            else
            {
                container.ClearValue(FrameworkElement.WidthProperty);
                container.MaxWidth = this.MaxItemWidth;
                Debug.WriteLine($"[ApplyLayoutResults] (Other) Tab '{header}' idx={output.Index}: Width cleared, MaxWidth={this.MaxItemWidth}, MinWidth={container.MinWidth}");
            }

            container.IsCompact = this.TabWidthPolicy == TabWidthPolicy.Compact && !output.IsPinned;
        }

        Debug.WriteLine("ApplyLayoutResults: Completed");

        // Force ItemsRepeater to re-measure after widths are set, to avoid oversizing
        if (this.regularItemsRepeater != null)
        {
            this.regularItemsRepeater.InvalidateMeasure();
            Debug.WriteLine($"[ApplyLayoutResults] Called InvalidateMeasure on regularItemsRepeater");
            this.regularItemsRepeater.UpdateLayout();
            Debug.WriteLine($"[ApplyLayoutResults] After UpdateLayout: regularItemsRepeater.ActualWidth={this.regularItemsRepeater.ActualWidth}, scrollHost.ActualWidth={this.scrollHost?.ActualWidth ?? -1}");
        }
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

    // Recompute and apply tab widths for new size
    this.UpdateTabWidths();
    this.UpdateOverflowButtonVisibility();

    // Log size change for troubleshooting
    this.LogSizeChanged(e.NewSize.Width, e.NewSize.Height);
    }
}
