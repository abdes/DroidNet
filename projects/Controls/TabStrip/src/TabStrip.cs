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
///    A lightweight, reusable tab strip control for WinUI 3 that displays a dynamic row of tabs
///    and raises events or executes commands when tabs are invoked, selected, or closed.
/// </summary>
[TemplatePart(Name = PartPinnedItemsRepeaterName, Type = typeof(ItemsRepeater))]
[TemplatePart(Name = PartRegularItemsRepeaterName, Type = typeof(ItemsRepeater))]
[TemplatePart(Name = PartScrollHostName, Type = typeof(ScrollViewer))]
[SuppressMessage("Design", "CA1001:Types that own disposable fields should be disposable", Justification = "Dispatcher-backed proxy fields are disposed in the Unloaded handler to align with control lifetime.")]
public partial class TabStrip : Control
{
    /// <summary>Logical name of the template part that hosts the control root grid.</summary>
    public const string RootGridPartName = "PartRootGrid";

    /// <summary>Logical name of the template part for the left overflow repeat button.</summary>
    public const string PartOverflowLeftButtonName = "PartOverflowLeftButton";

    /// <summary>Logical name of the template part for the right overflow repeat button.</summary>
    public const string PartOverflowRightButtonName = "PartOverflowRightButton";

    /// <summary>Logical name of the template part that contains pinned items.</summary>
    public const string PartPinnedItemsRepeaterName = "PartPinnedItemsRepeater";

    /// <summary>Logical name of the template part that contains regular (unpinned) items.</summary>
    public const string PartRegularItemsRepeaterName = "PartRegularItemsRepeater";

    /// <summary>Logical name of the template part that supplies horizontal scrolling.</summary>
    public const string PartScrollHostName = "PartScrollHost";

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

    // Cache of the last computed layout result to allow applying authoritative
    // widths to containers as they are prepared (prevents visual flicker).
    private LayoutResult? lastLayoutResult;

    /// <summary>
    ///    Initializes a new instance of the <see cref="TabStrip" /> class.
    /// </summary>
    /// <remarks>
    ///    The constructor prepares the internal read-only Items collection, creates
    ///    dispatcher-backed collection proxies for pinned and regular views, and initializes the
    ///    layout manager with current dependency-property values.
    /// </remarks>
    public TabStrip()
    {
        this.DefaultStyleKey = typeof(TabStrip);

        // Setup the Items collection, which is created internally and exposed
        // as a read-only property. The `Items` collection object never changes
        // for this control; only its contents change.
        var items = new ObservableCollection<TabItem>();
        items.CollectionChanged += this.OnItemsCollectionChanged;
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
        this.LayoutManager.LoggerFactory = this.LoggerFactory;

        // Handle Unloaded to dispose of disposable fields
        this.Unloaded += this.TabStrip_Unloaded;
    }

    /// <summary>
    ///    Gets or sets layout manager used to compute tab widths and layout decisions.
    /// </summary>
    /// <remarks>
    ///    The property is protected so tests can inject a deterministic or stub implementation.
    ///    Consumers should treat the layout manager as an implementation detail used to calculate
    ///    sizes and should not depend on its internal state.
    /// </remarks>
    protected TabStripLayoutManager LayoutManager { get; set; } = new TabStripLayoutManager();

    /// <summary>
    ///    Called when the control template is applied. Initializes template parts and synchronizes
    ///    template state with current dependency-property values.
    /// </summary>
    /// <remarks>
    ///    This method wires up required template parts (repeaters, buttons, scroll host) and
    ///    invokes <see cref="SyncTemplateStateAfterApply"/> so that properties set before template
    ///    application (for example selection or layout policy) are reflected in the visual tree. It
    ///    intentionally avoids exposing details about how each part attaches handlers; callers
    ///    should rely on the control's public surface and events rather than inspecting template
    ///    wiring.
    /// </remarks>
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        this.LogApplyingTemplate();

        // Setup each template part via small reusable methods
        this.InitializeRootGridPart();
        this.InitializeOverflowLeftButtonPart();
        this.InitializeOverflowRightButtonPart();
        this.InitializePinnedItemsRepeaterPart();
        this.InitializeRegularItemsRepeaterPart();
        this.InitializeScrollHostPart();

        // Apply behaviors based on current property values and ensure the
        // freshly-applied template reflects any properties that were set
        // prior to template application (layout, selection, itemsources, etc.).
        this.SyncTemplateStateAfterApply();
    }

    /// <summary>
    ///    Handles the Tapped event for a tab element. Executes the associated command, selects the
    ///    tab, and raises the <see cref="TabActivated"/> event.
    /// </summary>
    /// <param name="sender">
    ///     The source of the event, expected to be a <see cref="FrameworkElement"/> with a <see
    ///     cref="TabItem"/> as its DataContext.
    /// </param>
    /// <param name="e">The event data for the tap event.</param>
    /// <remarks>
    ///    This protected virtual method centralizes the action taken when a tab is invoked so
    ///    subclasses can override selection/command behavior while preserving the control's public
    ///    events. It performs only high-level operations (command invocation and selection) and
    ///    does not expose internal container wiring.
    /// </remarks>
    protected virtual void OnTabElementTapped(object? sender, TappedRoutedEventArgs e)
    {
        if (sender is FrameworkElement fe && fe.DataContext is TabItem ti)
        {
            // Invoke command if present
            ti.Command?.Execute(ti.CommandParameter);

            // Select the tab (centralized logic)
            this.SelectedItem = ti;  // This triggers OnSelectedItemChanged

            // Raise TabInvoked event
            this.TabActivated?.Invoke(this, new TabActivatedEventArgs { Item = ti, Index = this.SelectedIndex, Parameter = ti.CommandParameter });
            this.LogTabInvoked(ti);
        }
    }

    private T? GetTemplatePart<T>(string name, bool isRequired = false)
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

    private T GetRequiredTemplatePart<T>(string name)
        where T : DependencyObject
        => this.GetTemplatePart<T>(name, isRequired: true)!;

    private void InitializeRootGridPart()
    {
        if (this.rootGrid is not null)
        {
            this.rootGrid.SizeChanged -= this.OnTabStripSizeChanged;
        }

        this.rootGrid = this.GetRequiredTemplatePart<Grid>(RootGridPartName);
        this.rootGrid.SizeChanged += this.OnTabStripSizeChanged;
    }

    private void InitializeOverflowLeftButtonPart()
    {
        if (this.overflowLeftButton is not null)
        {
            this.overflowLeftButton.Click -= this.OnOverflowLeftButtonClick;
        }

        this.overflowLeftButton = this.GetTemplatePart<RepeatButton>(PartOverflowLeftButtonName);
        this.overflowLeftButton?.Click += this.OnOverflowLeftButtonClick;
    }

    private void InitializeOverflowRightButtonPart()
    {
        if (this.overflowRightButton is not null)
        {
            this.overflowRightButton.Click -= this.OnOverflowRightButtonClick;
        }

        this.overflowRightButton = this.GetTemplatePart<RepeatButton>(PartOverflowRightButtonName);
        this.overflowRightButton?.Click += this.OnOverflowRightButtonClick;
    }

    private void InitializePinnedItemsRepeaterPart()
    {
        if (this.pinnedItemsRepeater is not null)
        {
            this.pinnedItemsRepeater.ElementPrepared -= this.OnItemsRepeaterElementPrepared;
            this.pinnedItemsRepeater.ElementClearing -= this.OnItemsRepeaterElementClearing;
        }

        this.pinnedItemsRepeater = this.GetRequiredTemplatePart<ItemsRepeater>(PartPinnedItemsRepeaterName);
        this.pinnedItemsRepeater.ElementPrepared += this.OnItemsRepeaterElementPrepared;
        this.pinnedItemsRepeater.ElementClearing += this.OnItemsRepeaterElementClearing;
        this.pinnedItemsRepeater.ItemsSource = this.PinnedItemsView;
    }

    private void InitializeRegularItemsRepeaterPart()
    {
        if (this.regularItemsRepeater is not null)
        {
            this.regularItemsRepeater.ElementPrepared -= this.OnItemsRepeaterElementPrepared;
            this.regularItemsRepeater.ElementClearing -= this.OnItemsRepeaterElementClearing;
        }

        this.regularItemsRepeater = this.GetRequiredTemplatePart<ItemsRepeater>(PartRegularItemsRepeaterName);
        this.regularItemsRepeater.ElementPrepared += this.OnItemsRepeaterElementPrepared;
        this.regularItemsRepeater.ElementClearing += this.OnItemsRepeaterElementClearing;
        this.regularItemsRepeater.ItemsSource = this.RegularItemsView;
    }

    private void InitializeScrollHostPart()
    {
        if (this.scrollHost is not null)
        {
            this.scrollHost.ViewChanged -= this.OnScrollHostViewChanged;
            this.scrollHost.SizeChanged -= this.OnTabStripSizeChanged;
        }

        this.scrollHost = this.GetRequiredTemplatePart<ScrollViewer>(PartScrollHostName);
        this.scrollHost.ViewChanged += this.OnScrollHostViewChanged;
        this.scrollHost.SizeChanged += this.OnTabStripSizeChanged;
    }

    private void OnItemsRepeaterElementPrepared(ItemsRepeater sender, ItemsRepeaterElementPreparedEventArgs args)
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

        // Apply any cached authoritative layout result
        this.ApplyCachedLayoutResult(ti, tsi);

        // Recompute and apply tab widths after an element is prepared. This covers
        // the case where an item was just pinned/unpinned and moved between the
        // repeaters. Defer to the DispatcherQueue to avoid running during
        // repeater preparation (prevents reentrancy).
        _ = this.DispatcherQueue.TryEnqueue(this.RecalculateAndApplyTabWidths);
    }

    /// <summary>
    ///    If a cached authoritative layout result is available, apply the stored width and compact
    ///    state to the prepared container. Helps with reducing flicker and jumpy layout.
    /// </summary>
    private void ApplyCachedLayoutResult(TabItem ti, TabStripItem tsi)
    {
        if (this.lastLayoutResult is null)
        {
            return;
        }

        var idx = this.Items.IndexOf(ti);
        foreach (var outItem in this.lastLayoutResult.Items)
        {
            if (outItem.Index != idx)
            {
                continue;
            }

            if (this.TabWidthPolicy is TabWidthPolicy.Equal or TabWidthPolicy.Compact)
            {
                tsi.Width = outItem.Width;
                tsi.MaxWidth = this.MaxItemWidth;
            }
            else
            {
                tsi.ClearValue(FrameworkElement.WidthProperty);
                tsi.MaxWidth = this.MaxItemWidth;
            }

            tsi.IsCompact = this.TabWidthPolicy == TabWidthPolicy.Compact && !outItem.IsPinned;
            break;
        }
    }

    private void OnItemsRepeaterElementClearing(ItemsRepeater sender, ItemsRepeaterElementClearingEventArgs args)
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
        this.PointerWheelChanged -= this.OnScrollHostPointerWheelChanged;
        if (newValue)
        {
            this.PointerWheelChanged += this.OnScrollHostPointerWheelChanged;
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

        // Ensure prepared containers reflect the new selection immediately.
        // This updates visual container state (IsSelected) for already-prepared
        // items without forcing a complete recompute of widths.
        this.UpdatePreparedContainersState();
    }

    // Called from the TabWidthPolicy dependency property change callback.
    private void OnTabWidthPolicyChanged(TabWidthPolicy newPolicy)
    {
        this.LogTabWidthPolicyChanged(newPolicy);

        // Sync to layout manager
        this.LayoutManager.Policy = newPolicy;

        // Update prepared containers' state (compact/selected) centrally
        this.UpdatePreparedContainersState();

        // Recompute layout to reflect the new policy
        this.RecalculateAndApplyTabWidths();
    }

    private void OnOverflowLeftButtonClick(object? sender, RoutedEventArgs e)
    {
        if (this.scrollHost is null)
        {
            return;
        }

        var target = Math.Max(0, (int)(this.scrollHost.HorizontalOffset - 100));
        _ = this.scrollHost.ChangeView(target, verticalOffset: null, zoomFactor: null);
        this.LogOverflowLeftClicked();
    }

    private void OnOverflowRightButtonClick(object? sender, RoutedEventArgs e)
    {
        if (this.scrollHost is null)
        {
            return;
        }

        var target = (int)(this.scrollHost.HorizontalOffset + 100);
        _ = this.scrollHost.ChangeView(target, verticalOffset: null, zoomFactor: null);
        this.LogOverflowRightClicked();
    }

    private void OnScrollHostViewChanged(object? sender, ScrollViewerViewChangedEventArgs e)
        => this.UpdateOverflowButtonVisibility();

    private void OnScrollHostPointerWheelChanged(object? sender, PointerRoutedEventArgs e)
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

    private void RecalculateAndApplyTabWidths()
    {
        this.LogRecomputeStart();

        // Sync width properties to layout manager
        this.LayoutManager.MaxItemWidth = this.MaxItemWidth;
        this.LayoutManager.PreferredItemWidth = this.PreferredItemWidth;

        var containers = this.CollectPreparedContainers();
        if (containers.Count == 0)
        {
            this.LogRecomputeNoContainers();
            return;
        }

        var desiredWidths = this.MeasureDesiredWidthsForCompactPolicy(containers);
        var inputs = this.BuildLayoutInputs(containers, desiredWidths);
        var result = this.ComputeLayout(inputs);

        // Cache the computed layout so newly prepared containers can apply the
        // authoritative width immediately and avoid visual flicker.
        this.lastLayoutResult = result;
        this.ApplyLayoutResult(result, containers);

        this.LogRecomputeCompleted();
    }

    private Dictionary<int, TabStripItem> CollectPreparedContainers()
    {
        var indexToContainer = new Dictionary<int, TabStripItem>();
        this.CollectFromRepeater(this.pinnedItemsRepeater, indexToContainer);
        this.CollectFromRepeater(this.regularItemsRepeater, indexToContainer);
        this.LogCollectPreparedContainers(indexToContainer.Count);

        return indexToContainer;
    }

    /// <summary>
    ///    Apply compact and selection state to any currently prepared containers.
    ///    This centralizes logic so callers can ensure prepared visuals match
    ///    the control's DP values without duplicating iteration logic.
    /// </summary>
    private void UpdatePreparedContainersState()
    {
        var containers = this.CollectPreparedContainers();
        foreach (var kvp in containers)
        {
            var tsi = kvp.Value;
            if (tsi.DataContext is TabItem ti)
            {
                tsi.IsCompact = this.TabWidthPolicy == TabWidthPolicy.Compact && !ti.IsPinned;
                ti.IsSelected = ReferenceEquals(ti, this.SelectedItem);
            }
        }
    }

    private void CollectFromRepeater(ItemsRepeater? repeater, Dictionary<int, TabStripItem> indexToContainer)
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
                if (tsi?.DataContext is TabItem ti)
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

    private void InvalidateRepeatersAndLayout()
    {
        this.pinnedItemsRepeater?.InvalidateMeasure();
        this.regularItemsRepeater?.InvalidateMeasure();
        this.UpdateLayout();
    }

    private Dictionary<TabStripItem, double> MeasureDesiredWidthsForCompactPolicy(Dictionary<int, TabStripItem> containers)
    {
        // Measure desired widths only used for Compact policy
        var desiredWidths = new Dictionary<TabStripItem, double>();
        if (this.TabWidthPolicy != TabWidthPolicy.Compact)
        {
            this.LogMeasurePolicyNotCompact();
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

            // Intentionally do not log per-item measurements to avoid noisy output
            desiredWidths[tsi] = Math.Min(this.MaxItemWidth, Math.Max(effectiveMin, desired));

            // Restore original values
            if (origWidth != DependencyProperty.UnsetValue)
            {
                tsi.SetValue(FrameworkElement.WidthProperty, origWidth);
            }

            if (origMax != DependencyProperty.UnsetValue)
            {
                tsi.SetValue(FrameworkElement.MaxWidthProperty, origMax);
            }

            if (origMin != DependencyProperty.UnsetValue)
            {
                tsi.SetValue(FrameworkElement.MinWidthProperty, origMin);
            }
        }

        this.LogMeasuredDesiredWidths(desiredWidths.Count);

        return desiredWidths;
    }

    private List<LayoutPerItemInput> BuildLayoutInputs(Dictionary<int, TabStripItem> containers, Dictionary<TabStripItem, double> desiredWidths)
    {
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

        this.LogBuildLayoutInputs(inputs.Count);

        return inputs;
    }

    private LayoutResult ComputeLayout(List<LayoutPerItemInput> inputs)
    {
        // Compute layout: gather available width and inputs; log summary below
        // Calculate precise available width for tabs inside the ScrollViewer
        var availableWidth = this.scrollHost?.ActualWidth ?? 0;

        // The ItemsRepeater for regular tabs is the only child of the ScrollViewer
        // The StackLayout for ItemsRepeater may have a variable Spacing; read it at runtime
        // We do NOT subtract overflow button widths, as they are overlaid
        // We do NOT subtract vertical scrollbar width, as vertical scroll is disabled
        // Add a small fudge factor to avoid rounding errors
        const double fudge = 1.0;

        // Always try to subtract tab spacing if we have a StackLayout and more than one tab
        var stackLayout = this.regularItemsRepeater?.Layout as StackLayout;
        if (stackLayout != null && inputs.Count > 1)
        {
            var count = inputs.Count;
            var spacing = stackLayout.Spacing * (count - 1);
            availableWidth -= spacing;

            // Subtract tab spacing from available width for layout calculations
        }

        availableWidth = Math.Max(0, availableWidth - fudge);
        this.LogComputeLayout(availableWidth, inputs.Count);

        var request = new LayoutRequest(availableWidth, inputs);
        var result = this.LayoutManager.ComputeLayout(request);

        return result;
    }

    private void ApplyLayoutResult(LayoutResult result, Dictionary<int, TabStripItem> containers)
    {
        // Apply layout results to prepared containers
        foreach (var output in result.Items)
        {
            if (!containers.TryGetValue(output.Index, out var container))
            {
                this.LogNoContainerForIndex(output.Index);
                continue;
            }

            container.MinWidth = Math.Min(container.MinWidth, this.MaxItemWidth);

            if (this.TabWidthPolicy == TabWidthPolicy.Equal)
            {
                container.Width = output.Width;
                container.MaxWidth = this.MaxItemWidth;

                // Applied equal width to container
            }
            else if (this.TabWidthPolicy == TabWidthPolicy.Compact)
            {
                container.Width = output.Width;
                container.MaxWidth = this.MaxItemWidth;

                // Applied compact width to container
            }
            else
            {
                container.ClearValue(FrameworkElement.WidthProperty);
                container.MaxWidth = this.MaxItemWidth;

                // Cleared explicit width for container (other policy)
            }

            container.IsCompact = this.TabWidthPolicy == TabWidthPolicy.Compact && !output.IsPinned;
        }

        // Force ItemsRepeater to re-measure after widths are set, to avoid oversizing
        if (this.regularItemsRepeater != null)
        {
            this.regularItemsRepeater.InvalidateMeasure();
            this.regularItemsRepeater.UpdateLayout();

            // Log aggregate tab sizing info
            var calcWidth = result.Items.Count > 0 ? (double?)result.Items[0].Width : null;
            this.LogTabSizing(this.TabWidthPolicy, calcWidth, this.MaxItemWidth, result.Items.Count, this.scrollHost?.ActualWidth ?? 0);
        }
    }

    private void OnItemsCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        var items = this.Items as ObservableCollection<TabItem>;

        // Delegate work to smaller helpers for clarity and testability
        if (e.Action == NotifyCollectionChangedAction.Add && e.NewItems is not null && items is not null && !this.suppressCollectionChangeHandling)
        {
            this.HandleAddedItems(items, e);
        }
        else if (e.Action == NotifyCollectionChangedAction.Add && e.NewItems is not null)
        {
            this.LogAddedItemsFallback(items, e);
        }

        // Re-apply widths when tab collection changes
        this.InvalidateRepeatersAndLayout();

        if (e.Action == NotifyCollectionChangedAction.Remove && e.OldItems is not null && items is not null && !this.suppressCollectionChangeHandling)
        {
            this.HandleRemovedItems(items, e);
        }

        this.EnsureSelectedItem(items);

        _ = this.DispatcherQueue.TryEnqueue(this.UpdateOverflowButtonVisibility);
    }

    // Handle additions: compute desired index, log, and defer move/selection to the dispatcher
    private void HandleAddedItems(ObservableCollection<TabItem> items, NotifyCollectionChangedEventArgs e)
    {
        foreach (var newItem in e.NewItems!)
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

                this.DeferMoveAndSelect(items, ti, desiredIndex);
            }
        }
    }

    // Enqueue move + selection logic so it runs after the CollectionChanged event completes
    private void DeferMoveAndSelect(ObservableCollection<TabItem> items, TabItem ti, int desiredIndex)
        => _ = this.DispatcherQueue.TryEnqueue(() =>
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
            this.InvalidateRepeatersAndLayout();

            _ = this.DispatcherQueue.TryEnqueue(this.UpdateOverflowButtonVisibility);
        });

    // Fallback logging for Add actions when the primary handler didn't run
    private void LogAddedItemsFallback(ObservableCollection<TabItem>? items, NotifyCollectionChangedEventArgs e)
    {
        foreach (var newItem in e.NewItems!)
        {
            if (newItem is TabItem ti)
            {
                var count = items is not null ? items.Count : -1;
                this.LogItemAdded(ti, count);
            }
        }
    }

    // Handle removals: if the selected item was removed, choose a neighbor
    private void HandleRemovedItems(ObservableCollection<TabItem> items, NotifyCollectionChangedEventArgs e)
    {
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

    private void EnsureSelectedItem(ObservableCollection<TabItem>? items)
    {
        if (!this.suppressCollectionChangeHandling && items?.Count > 0 && this.SelectedItem is null)
        {
            this.SelectedItem = items[0];
        }
    }

    private void OnTabStripSizeChanged(object? sender, SizeChangedEventArgs e)
    {
        this.LogSizeChanged(e.NewSize.Width, e.NewSize.Height);

        // Recompute and apply tab widths for new size
        this.RecalculateAndApplyTabWidths();
        this.UpdateOverflowButtonVisibility();

        // Log size change for troubleshooting
        this.LogSizeChanged(e.NewSize.Width, e.NewSize.Height);
    }

    // Ensures template parts reflect the current dependency property state
    // when the template is applied after properties changed. This method is
    // intentionally conservative: it re-syncs the layout manager, re-assigns
    // ItemsSource on repeaters, restores pointer-wheel behavior, and schedules
    // layout/width computation after repeaters have prepared their containers.
    private void SyncTemplateStateAfterApply()
    {
        // Sync layout manager with current DP values
        this.LayoutManager.MaxItemWidth = this.MaxItemWidth;
        this.LayoutManager.PreferredItemWidth = this.PreferredItemWidth;
        this.LayoutManager.Policy = this.TabWidthPolicy;

        // Repeaters were assigned their ItemsSource in the Setup* methods above
        // so we don't need to reassign them here. Keeping assignment only in
        // the setup methods avoids redundant operations.

        // Restore pointer-wheel scrolling behavior according to the current DP
        this.OnScrollOnWheelChanged(this.ScrollOnWheel);

        // Schedule layout-related operations on the dispatcher so that
        // ItemsRepeater can finish preparing containers first. This avoids
        // running RecomputeAndApplyTabWidths synchronously during repeater preparation.
        _ = this.DispatcherQueue.TryEnqueue(() =>
        {
            // Recompute widths and update prepared container state once repeaters
            // have prepared their elements.
            this.RecalculateAndApplyTabWidths();
            this.UpdatePreparedContainersState();

            // Allow layout to settle and then update overflow visibility
            _ = this.DispatcherQueue.TryEnqueue(this.UpdateOverflowButtonVisibility);
        });

        // Try to update overflow visibility immediately in case layout is already settled.
        this.UpdateOverflowButtonVisibility();
    }
}
