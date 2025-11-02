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
using Canvas = Microsoft.UI.Xaml.Controls.Canvas;

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

    /// <summary>Logical name of the template part used as an overlay for reorder placeholder visuals.</summary>
    public const string PartReorderOverlayName = "PartReorderOverlay";

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
    private Canvas? reorderOverlay;

    // Placeholder lifecycle state for reorder mode (Phase 3)
    private TabItem? reorderPlaceholder;  // Placeholder TabItem inserted into RegularItemsView during reordering
    private int reorderPlaceholderBucketIndex = -1;

    private ILogger? logger;

    // Cache of the last computed layout result to allow applying authoritative
    // widths to containers as they are prepared (prevents visual flicker).
    private LayoutResult? lastLayoutResult;

    // Stored pointer event handlers for proper cleanup
    private PointerEventHandler? pointerPressedHandler;
    private PointerEventHandler? pointerMovedHandler;
    private PointerEventHandler? pointerReleasedHandler;

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
        var regularViewLocal = new FilteredObservableCollection<TabItem>(items, ti => !ti.IsPinned, [nameof(TabItem.IsPinned)]);

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
        this.regularProxy = new DispatcherCollectionProxy<TabItem>(regularViewLocal, regularViewLocal, Enqueue);

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
    ///     Inserts a placeholder at the position of the specified dragged item in the Items collection.
    ///     The placeholder visually replaces the dragged item (which remains in the collection but is transformed).
    /// </summary>
    /// <param name="draggedItem">The item being dragged.</param>
    /// <returns>The bucket index (in regular items) where placeholder was inserted, or -1 on failure.</returns>
    internal int InsertPlaceholderAtDraggedItemPosition(TabItem draggedItem)
    {
        this.AssertUIThread();

        if (this.Items is not ObservableCollection<TabItem> items || draggedItem?.IsPinned != false)
        {
            return -1;
        }

        // Find the dragged item in the Items collection using reference equality (avoid relying on Equals overrides)
        var draggedItemIndex = -1;
        for (var i = 0; i < items.Count; i++)
        {
            if (ReferenceEquals(items[i], draggedItem))
            {
                draggedItemIndex = i;
                break;
            }
        }

        if (draggedItemIndex < 0)
        {
            return -1;
        }

        // Create placeholder and insert at the same position
        this.reorderPlaceholder = this.CreatePlaceholderItem();

        try
        {
            this.suppressCollectionChangeHandling = true;
            items.Insert(draggedItemIndex, this.reorderPlaceholder);
        }
        finally
        {
            this.suppressCollectionChangeHandling = false;
        }

        // Calculate and store the bucket index (position in regular items, excluding pinned and placeholder)
        var bucketIndex = 0;
        for (var i = 0; i < items.Count; i++)
        {
            if (ReferenceEquals(items[i], this.reorderPlaceholder))
            {
                break; // reached placeholder - bucket index is count of regular items to the left
            }

            if (!items[i].IsPinned && !items[i].IsPlaceholder)
            {
                bucketIndex++;
            }
        }

        this.reorderPlaceholderBucketIndex = bucketIndex;
        return bucketIndex;
    }

    /// <summary>
    ///     Swaps the placeholder with an adjacent item in the specified direction.
    ///     Used when the pointer crosses more than half of an adjacent item's width.
    /// </summary>
    /// <param name="swapForward">True to swap forward (right), false to swap backward (left).</param>
    /// <returns>True if swap occurred, false otherwise.</returns>
    internal bool SwapPlaceholderWithAdjacentItem(bool swapForward)
    {
        this.AssertUIThread();

        if (this.Items is not ObservableCollection<TabItem> items || this.reorderPlaceholder == null)
        {
            return false;
        }

        var placeholderIndex = items.IndexOf(this.reorderPlaceholder);
        if (placeholderIndex < 0)
        {
            return false;
        }

        // Find the adjacent regular (non-pinned) item in the specified direction
        var step = swapForward ? 1 : -1;
        var adjacentIndex = placeholderIndex + step;

        while (adjacentIndex >= 0 && adjacentIndex < items.Count)
        {
            var adjacentItem = items[adjacentIndex];

            // Skip pinned items
            if (!adjacentItem.IsPinned)
            {
                // Found the adjacent regular item - swap with placeholder
                try
                {
                    this.suppressCollectionChangeHandling = true;
                    items.Move(placeholderIndex, adjacentIndex);

                    // Update bucket index
                    this.reorderPlaceholderBucketIndex = swapForward
                        ? this.reorderPlaceholderBucketIndex + 1
                        : this.reorderPlaceholderBucketIndex - 1;

                    return true;
                }
                finally
                {
                    this.suppressCollectionChangeHandling = false;
                }
            }

            adjacentIndex += step;
        }

        return false;
    }

    /// <summary>
    ///     Removes the placeholder from the Items collection.
    ///     The dragged item (which was after the placeholder) will naturally slide into the placeholder's position.
    /// </summary>
    /// <returns>The Items index where the placeholder was removed, or -1 if no placeholder existed.</returns>
    internal int RemovePlaceholder()
    {
        this.AssertUIThread();

        if (this.Items is not ObservableCollection<TabItem> items || this.reorderPlaceholder is null)
        {
            return -1;
        }

        var placeholderIndex = items.IndexOf(this.reorderPlaceholder);
        if (placeholderIndex < 0)
        {
            this.reorderPlaceholder = null;
            this.reorderPlaceholderBucketIndex = -1;
            return -1;
        }

        try
        {
            this.suppressCollectionChangeHandling = true;
            items.RemoveAt(placeholderIndex);
        }
        finally
        {
            this.suppressCollectionChangeHandling = false;
        }

        this.reorderPlaceholder = null;
        this.reorderPlaceholderBucketIndex = -1;
        return placeholderIndex;
    }

    /// <summary>
    ///     Commits a reorder operation by moving the specified dragged item to the placeholder's
    ///     current position, then removing the placeholder. This performs the definitive mutation
    ///     of the Items collection to reflect the user's final drop location.
    /// </summary>
    /// <param name="draggedItem">The TabItem being dragged (must be present in Items).</param>
    /// <returns>The new index of the dragged item after commit, or -1 if commit could not be performed.</returns>
    internal int CommitReorderFromPlaceholder(TabItem draggedItem)
    {
        this.AssertUIThread();

        if (this.Items is not ObservableCollection<TabItem> items || this.reorderPlaceholder is null || draggedItem is null)
        {
            return -1;
        }

        var placeholderIndex = items.IndexOf(this.reorderPlaceholder);
        var draggedIndex = items.IndexOf(draggedItem);
        if (placeholderIndex < 0 || draggedIndex < 0)
        {
            return -1;
        }

        // Remove the placeholder first, then move the dragged item to the placeholder's former slot.
        // After removal, indices >= placeholderIndex shift down by 1.
        try
        {
            this.suppressCollectionChangeHandling = true;
            items.RemoveAt(placeholderIndex);

            // Adjust dragged index if it was after the removed placeholder
            if (draggedIndex > placeholderIndex)
            {
                draggedIndex--;
            }

            var targetIndex = placeholderIndex <= draggedIndex ? placeholderIndex : placeholderIndex - 1;

            if (draggedIndex != targetIndex && targetIndex >= 0 && targetIndex < items.Count)
            {
                items.Move(draggedIndex, targetIndex);
                draggedIndex = targetIndex;
            }
        }
        finally
        {
            this.suppressCollectionChangeHandling = false;
            this.reorderPlaceholder = null;
            this.reorderPlaceholderBucketIndex = -1;
        }

        return draggedIndex;
    }

    /// <summary>
    ///     Gets the current bucket index (position in regular items) of the placeholder.
    /// </summary>
    /// <returns>Bucket index, or -1 if no placeholder exists.</returns>
    internal int GetPlaceholderBucketIndex() => this.reorderPlaceholderBucketIndex;

    /// <summary>
    ///     Gets the regular (unpinned) items repeater for drag operations.
    /// </summary>
    /// <returns>The ItemsRepeater for regular items, or null if not available.</returns>
    internal ItemsRepeater? GetRegularItemsRepeater() => this.regularItemsRepeater;

    /// <summary>
    ///     Converts a point in physical screen coordinates to TabStrip-relative logical coordinates.
    /// </summary>
    /// <param name="physicalScreenPoint">The point in **PHYSICAL SCREEN PIXELS** (e.g., from GetCursorPos).</param>
    /// <returns>The converted point relative to this TabStrip in logical DIPs.</returns>
    internal Windows.Foundation.Point ScreenToStrip(Windows.Foundation.Point physicalScreenPoint)
    {
        // Get the TabStrip's physical screen bounds
        var physicalBounds = Native.GetPhysicalScreenBoundsUsingWindowRect(this);
        if (physicalBounds == null)
        {
            // Fallback: assume point is already relative (shouldn't happen)
            return physicalScreenPoint;
        }

        // Get DPI to convert from physical to logical
        var dpi = Native.GetDpiFromXamlRoot(this.XamlRoot);

        // Convert physical screen point to strip-relative physical coordinates
        var physicalRelativeX = physicalScreenPoint.X - physicalBounds.Value.Left;
        var physicalRelativeY = physicalScreenPoint.Y - physicalBounds.Value.Top;

        // Convert from physical pixels to logical DIPs
        var logicalX = Native.PhysicalToLogical((int)physicalRelativeX, dpi);
        var logicalY = Native.PhysicalToLogical((int)physicalRelativeY, dpi);

        return new Windows.Foundation.Point(logicalX, logicalY);
    }

    /// <summary>
    ///     Converts a TabStrip-relative logical point to physical screen coordinates.
    /// </summary>
    /// <param name="stripPoint">The point relative to this TabStrip in logical DIPs.</param>
    /// <returns>The converted point in **PHYSICAL SCREEN PIXELS**.</returns>
    internal Windows.Foundation.Point StripToScreen(Windows.Foundation.Point stripPoint)
    {
        // Get the TabStrip's physical screen bounds
        var physicalBounds = Native.GetPhysicalScreenBoundsUsingWindowRect(this);
        if (physicalBounds == null)
        {
            // Fallback: assume point is already in screen coordinates (shouldn't happen)
            return stripPoint;
        }

        // Get DPI to convert from logical to physical
        var dpi = Native.GetDpiFromXamlRoot(this.XamlRoot);

        // Convert from logical DIPs to physical pixels
        var physicalRelativeX = Native.LogicalToPhysical(stripPoint.X, dpi);
        var physicalRelativeY = Native.LogicalToPhysical(stripPoint.Y, dpi);

        // Convert strip-relative physical to screen physical coordinates
        var screenX = physicalBounds.Value.Left + physicalRelativeX;
        var screenY = physicalBounds.Value.Top + physicalRelativeY;

        return new Windows.Foundation.Point(screenX, screenY);
    }

    /// <summary>
    ///     Determines which regular item (by bucket index) the pointer is currently over,
    ///     considering the midpoint swap threshold.
    /// </summary>
    /// <param name="pointerX">X coordinate in TabStrip-relative logical pixels.</param>
    /// <returns>Bucket index of the target item, or -1 if outside regular bucket.</returns>
    internal int HitTestRegularBucket(double pointerX)
    {
        var count = this.GetRegularBucketCount();
        if (this.regularItemsRepeater == null || count == 0)
        {
            return -1;
        }

        var repeaterX = this.GetRepeaterRelativeX(pointerX);
        return this.FindBucketIndexAtRepeaterX(repeaterX, count);
    }

    /// <summary>
    ///     Maps a repeater index in the RegularItemsRepeater to the corresponding TabItem
    ///     in the Items collection by skipping pinned items.
    /// </summary>
    /// <param name="repeaterIndex">Index within the regular (unpinned) items repeater.</param>
    /// <returns>The corresponding TabItem, or null if not found.</returns>
    protected virtual TabItem? GetRegularItemForRepeaterIndex(int repeaterIndex)
    {
        if (this.Items is not ObservableCollection<TabItem> items)
        {
            return null;
        }

        var regularIndex = 0;
        for (var j = 0; j < items.Count; j++)
        {
            if (!items[j].IsPinned)
            {
                if (regularIndex == repeaterIndex)
                {
                    return items[j];
                }

                regularIndex++;
            }
        }

        return null;
    }

    /// <summary>
    ///     Gets the count of items in the unpinned (regular) bucket.
    /// </summary>
    /// <returns>The number of items currently in the unpinned bucket.</returns>
    protected virtual int GetRegularBucketCount() => this.RegularItemsView?.Count ?? 0;

    /// <summary>
    ///     Computes the X coordinate relative to the regular items repeater from a TabStrip-relative X.
    /// </summary>
    /// <param name="pointerX">The X coordinate in TabStrip-relative logical pixels.</param>
    /// <returns>The corresponding X coordinate in the RegularItemsRepeater's coordinate space.</returns>
    protected virtual double GetRepeaterRelativeX(double pointerX)
    {
        var repeaterOrigin = this.regularItemsRepeater!.TransformToVisual(this).TransformPoint(new Windows.Foundation.Point(0, 0));
        return pointerX - repeaterOrigin.X;
    }

    /// <summary>
    ///     Scans visible regular items to determine the target bucket index for insertion given a repeater-relative X.
    /// </summary>
    /// <param name="repeaterX">X coordinate relative to the RegularItemsRepeater.</param>
    /// <param name="count">Number of elements in the regular items repeater.</param>
    /// <returns>The bucket index excluding the placeholder.</returns>
    protected virtual int FindBucketIndexAtRepeaterX(double repeaterX, int count)
    {
        var layout = this.regularItemsRepeater!.Layout as StackLayout;
        var spacing = layout?.Spacing ?? 0;

        var accumulatedX = 0.0;
        var bucketIndex = 0; // Index excluding placeholder

        for (var i = 0; i < count; i++)
        {
            var currentItem = this.GetRegularItemForRepeaterIndex(i);
            if (currentItem is null)
            {
                continue;
            }

            var isPlaceholder = ReferenceEquals(currentItem, this.reorderPlaceholder);
            if (this.regularItemsRepeater.TryGetElement(i) is not FrameworkElement element)
            {
                continue;
            }

            var width = element.ActualWidth;
            var left = accumulatedX;
            var right = accumulatedX + width;
            var midpoint = (left + right) / 2.0;

            if (repeaterX < midpoint && !isPlaceholder)
            {
                return bucketIndex;
            }

            accumulatedX = right + spacing;

            if (!isPlaceholder)
            {
                bucketIndex++;
            }
        }

        return bucketIndex; // after last item
    }

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
        this.InitializeReorderOverlayPart();

        // Wire up drag detection after template is applied
        this.InstallDragPointerHandlers();

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

    /// <summary>
    ///     Creates a new placeholder TabItem with appropriate properties.
    /// </summary>
    /// <returns>A new TabItem configured as a placeholder.</returns>
    protected virtual TabItem CreatePlaceholderItem()
        => new()
        {
            Header = string.Empty,
            IsPlaceholder = true,
            IsClosable = false,
            IsPinned = false,
        };

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

/// <summary>
///     Ensures the reorder overlay canvas exists in the current template.
/// </summary>
private void EnsureReorderOverlay()
{
    if (this.reorderOverlay == null)
    {
        // Not fatal; simply no-op if template lacks the overlay (defensive)
    }
}

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

// No events are attached to the overlay; it simply hosts placeholder visuals.
private void InitializeReorderOverlayPart() =>
    this.reorderOverlay = this.GetTemplatePart<Canvas>(PartReorderOverlayName);

private void OnItemsRepeaterElementPrepared(ItemsRepeater sender, ItemsRepeaterElementPreparedEventArgs args)
{
    Debug.Assert(args.Element is FrameworkElement { DataContext: TabItem }, "control expects DataContext to be set with a TabItem, on each of its TabStrip items");

    // The element is a Grid wrapper (from ItemTemplate) containing a TabStripItem or placeholder Border
    if (args.Element is not Grid { DataContext: TabItem ti } wrapperGrid)
    {
        this.LogBadItem(sender, args.Element);
        return;
    }

    // If this is a placeholder item, skip all setup logic (placeholders are lightweight and never interactive)
    if (ti.IsPlaceholder)
    {
        return;
    }

    // Find the TabStripItem child within the Grid wrapper
    var tsi = wrapperGrid.Children.OfType<TabStripItem>().FirstOrDefault();
    if (tsi == null)
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
            tsi.ClearValue(WidthProperty);
            tsi.MaxWidth = this.MaxItemWidth;
        }

        tsi.IsCompact = this.TabWidthPolicy == TabWidthPolicy.Compact && !outItem.IsPinned;
        break;
    }
}

private void OnItemsRepeaterElementClearing(ItemsRepeater sender, ItemsRepeaterElementClearingEventArgs args)
{
    Debug.Assert(args.Element is Grid, "control expects each element to be a Grid wrapper from ItemTemplate");

    // The element is a Grid wrapper; find the TabStripItem child
    if (args.Element is not Grid wrapperGrid)
    {
        this.LogBadItem(sender, args.Element);
        return;
    }

    // If this is a placeholder, skip cleanup (no event handlers were attached)
    if (wrapperGrid.DataContext is TabItem { IsPlaceholder: true })
    {
        return;
    }

    var tsi = wrapperGrid.Children.OfType<TabStripItem>().FirstOrDefault();
    if (tsi == null)
    {
        // Placeholder Border, not a TabStripItem - no cleanup needed
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

    // Uninstall drag detection handlers
    this.UninstallDragPointerHandlers();

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

private void InstallDragPointerHandlers()
{
    // Wire up preview (tunneling) pointer events for drag detection after template is applied.
    // Using tunneling events with handledEventsToo: true ensures we receive pointer events
    // even when TabStripItem children handle them, allowing us to detect drag gestures.
    // Store handlers as fields for proper cleanup in Unloaded.
    this.pointerPressedHandler = new PointerEventHandler(this.OnPreviewPointerPressed);
    this.pointerMovedHandler = new PointerEventHandler(this.OnPreviewPointerMoved);
    this.pointerReleasedHandler = new PointerEventHandler(this.OnPreviewPointerReleased);

    this.AddHandler(PointerPressedEvent, this.pointerPressedHandler, handledEventsToo: true);
    this.AddHandler(PointerMovedEvent, this.pointerMovedHandler, handledEventsToo: true);
    this.AddHandler(PointerReleasedEvent, this.pointerReleasedHandler, handledEventsToo: true);
}

private void UninstallDragPointerHandlers()
{
    // Remove pointer event handlers that were installed in OnApplyTemplate
    if (this.pointerPressedHandler is not null)
    {
        this.RemoveHandler(PointerPressedEvent, this.pointerPressedHandler);
        this.pointerPressedHandler = null;
    }

    if (this.pointerMovedHandler is not null)
    {
        this.RemoveHandler(PointerMovedEvent, this.pointerMovedHandler);
        this.pointerMovedHandler = null;
    }

    if (this.pointerReleasedHandler is not null)
    {
        this.RemoveHandler(PointerReleasedEvent, this.pointerReleasedHandler);
        this.pointerReleasedHandler = null;
    }
}

// (moved reorder helper methods earlier in the file)

/// <summary>
///     Computes the left coordinate (TabStrip-relative) for the given unpinned-bucket slot index.
/// </summary>
private double GetBucketSlotLeft(int bucketIndex)
{
    var count = this.GetRegularBucketCount();
    if (this.regularItemsRepeater == null || count == 0)
    {
        return 0.0;
    }

    bucketIndex = Math.Max(0, Math.Min(bucketIndex, count));

    var layout = this.regularItemsRepeater.Layout as StackLayout;
    var spacing = layout?.Spacing ?? 0;

    // Calculate position relative to the regularItemsRepeater
    var left = 0.0;
    for (var i = 0; i < bucketIndex && i < count; i++)
    {
        if (this.regularItemsRepeater.TryGetElement(i) is FrameworkElement fe)
        {
            left += fe.ActualWidth + spacing;
        }
    }

    // Convert to TabStrip-relative coordinates for the overlay canvas
    var repeaterOrigin = this.regularItemsRepeater.TransformToVisual(this).TransformPoint(new Windows.Foundation.Point(0, 0));
    return left + repeaterOrigin.X;
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
            // TryGetElement returns the Grid wrapper from ItemTemplate
            var wrapper = repeater.TryGetElement(i) as Grid;
            if (wrapper?.DataContext is TabItem { IsPlaceholder: false } ti)
            {
                // Extract TabStripItem from Grid wrapper
                var tsi = wrapper.Children.OfType<TabStripItem>().FirstOrDefault();
                if (tsi != null)
                {
                    var index = this.Items.IndexOf(ti);
                    indexToContainer[index] = tsi;
                }
            }
        }
    }
    else
    {
        // Fallback: iterate visual tree
        var count = VisualTreeHelper.GetChildrenCount(repeater);
        for (var i = 0; i < count; i++)
        {
            var child = VisualTreeHelper.GetChild(repeater, i);
            if (child is Grid wrapper && wrapper.DataContext is TabItem { IsPlaceholder: false } ti)
            {
                var tsi = wrapper.Children.OfType<TabStripItem>().FirstOrDefault();
                if (tsi != null)
                {
                    var index = this.Items.IndexOf(ti);
                    indexToContainer[index] = tsi;
                }
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
        var origWidth = tsi.ReadLocalValue(WidthProperty);
        var origMax = tsi.ReadLocalValue(MaxWidthProperty);
        var origMin = tsi.ReadLocalValue(MinWidthProperty);
        tsi.ClearValue(WidthProperty);
        tsi.ClearValue(MaxWidthProperty);
        tsi.ClearValue(MinWidthProperty);

        tsi.Measure(new Windows.Foundation.Size(double.PositiveInfinity, double.PositiveInfinity));
        var desired = tsi.DesiredSize.Width;
        var effectiveMin = Math.Min(tsi.MinWidth, this.MaxItemWidth);

        // Intentionally do not log per-item measurements to avoid noisy output
        desiredWidths[tsi] = Math.Min(this.MaxItemWidth, Math.Max(effectiveMin, desired));

        // Restore original values
        if (origWidth != DependencyProperty.UnsetValue)
        {
            tsi.SetValue(WidthProperty, origWidth);
        }

        if (origMax != DependencyProperty.UnsetValue)
        {
            tsi.SetValue(MaxWidthProperty, origMax);
        }

        if (origMin != DependencyProperty.UnsetValue)
        {
            tsi.SetValue(MinWidthProperty, origMin);
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
            container.ClearValue(WidthProperty);
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

            // Skip selection/activation logic for placeholder items used during drag reordering
            if (ti.IsPlaceholder)
            {
                continue;
            }

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

void EnsureSelectedItem(ObservableCollection<TabItem>? items)
{
    if (!this.suppressCollectionChangeHandling && items?.Count > 0 && this.SelectedItem is null)
    {
        // Select the first non-placeholder item
        var firstNonPlaceholder = items.FirstOrDefault(ti => !ti.IsPlaceholder);
        if (firstNonPlaceholder is not null)
        {
            this.SelectedItem = firstNonPlaceholder;
        }
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
