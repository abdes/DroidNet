// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Drag;
using DroidNet.Collections;
using DroidNet.Coordinates;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Aura.Controls;

/// <summary>
///    A lightweight, reusable tab strip control for WinUI 3 that displays a dynamic row of tabs
///    and raises events or executes commands when tabs are invoked, selected, or closed.
/// </summary>
[TemplatePart(Name = PartPinnedItemsRepeaterName, Type = typeof(ItemsRepeater))]
[TemplatePart(Name = PartRegularItemsRepeaterName, Type = typeof(ItemsRepeater))]
[TemplatePart(Name = PartScrollHostName, Type = typeof(ScrollViewer))]
[SuppressMessage("Design", "CA1001:Types that own disposable fields should be disposable", Justification = "Dispatcher-backed proxy fields are disposed in the Unloaded handler to align with control lifetime.")]
public partial class TabStrip : Control, ITabStrip
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

    private readonly List<RealizedItemInfo> realizedItems = [];
    private ExternalInsertInfo? pendingExternalInsert;

    // Pending realization waiters keyed by TabItem.ContentId. Used so InsertItemAsync
    // can await a central notification rather than subscribing a per-call handler
    // to ItemsRepeater.ElementPrepared (avoids duplicate handlers and races).
    private readonly Dictionary<Guid, TaskCompletionSource<FrameworkElement?>> pendingRealizations = new();

    private DispatcherCollectionProxy<TabItem>? pinnedProxy;
    private DispatcherCollectionProxy<TabItem>? regularProxy;

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

    /// <inheritdoc/>
    public IReadOnlyList<TabStripItemSnapshot> TakeSnapshot()
    {
        return this.realizedItems
            .Where(info => !info.IsPinned)
            .Select(info =>
            {
                Debug.WriteLine($"Realized Element[{info.Index}]: {info.Element.Tag}");
                var point = info.Element.TransformToVisual(this).TransformPoint(new Windows.Foundation.Point(0, 0));
                return new TabStripItemSnapshot
                {
                    ItemIndex = info.Index,
                    LayoutOrigin = info.Element.TransformToVisual(this).TransformPoint(new Windows.Foundation.Point(0, 0)).AsElement(),
                    Width = info.Element.RenderSize.Width,
                    Container = info.Element,
                };
            })
            .OrderBy(s => s.LayoutOrigin.Point.X)
            .Select(s =>
            {
                Debug.WriteLine($"Snapshot[{s.ItemIndex}]: LayoutOrigin={s.LayoutOrigin}, Width={s.Width}");
                return s;
            })
            .ToList();
    }

    /// <inheritdoc/>
    public void ApplyTransformToItem(int itemIndex, double offsetX)
    {
        if (itemIndex < 0 || itemIndex >= this.Items.Count)
        {
            Debug.WriteLine($"[TabStrip] ApplyTransform skipped: itemIndex={itemIndex}, offset={offsetX}, reason=out of range");
            return;
        }

        // Use the realized items cache so we only touch prepared containers.
        Debug.WriteLine($"[TabStrip] ApplyTransform start: itemIndex={itemIndex}, offset={offsetX}, realizedCount={this.realizedItems.Count}");
        for (var i = 0; i < this.realizedItems.Count; i++)
        {
            var info = this.realizedItems[i];
            if (info.Index != itemIndex || info.IsPinned)
            {
                continue;
            }

            if (info.Element is not Grid grid)
            {
                Debug.WriteLine($"[TabStrip] ApplyTransform skip: realized element at index {info.Index} is not Grid");
                continue;
            }

            if (grid.RenderTransform is not TranslateTransform transform)
            {
                transform = new TranslateTransform();
                grid.RenderTransform = transform;
                Debug.WriteLine($"[TabStrip] ApplyTransform created new TranslateTransform for itemIndex={itemIndex}");
            }

            transform.X = offsetX;
            Debug.WriteLine($"[TabStrip] ApplyTransform applied offset: itemIndex={itemIndex}, offset={offsetX}");
            break;
        }

        Debug.WriteLine($"[TabStrip] ApplyTransform end: itemIndex={itemIndex}");
    }

    /// <inheritdoc/>
    public FrameworkElement? GetContainerForIndex(int index)
    {
        if (index < 0)
        {
            return null;
        }

        for (var i = 0; i < this.realizedItems.Count; i++)
        {
            var info = this.realizedItems[i];
            if (info.Index != index)
            {
                continue;
            }

            if (info.Element is Grid grid)
            {
                return grid.Children.OfType<TabStripItem>().FirstOrDefault();
            }
        }

        return null;
    }

    /// <inheritdoc/>
    public void RemoveItemAt(int index)
    {
        this.Items.RemoveAt(index);
    }

    /// <inheritdoc/>
    public void MoveItem(int fromIndex, int toIndex)
    {
        var items = this.Items;

        if (fromIndex < 0 || fromIndex >= items.Count)
        {
            return;
        }

        var clampedTarget = System.Math.Clamp(toIndex, 0, items.Count - 1);
        if (fromIndex == clampedTarget)
        {
            return;
        }

        var preservedSelection = this.SelectedItem;

        try
        {
            this.suppressCollectionChangeHandling = true;
            items.Move(fromIndex, clampedTarget);
            this.RefreshAllRealizedItemIndices();
        }
        finally
        {
            this.suppressCollectionChangeHandling = false;
        }

        if (preservedSelection is not null && !ReferenceEquals(this.SelectedItem, preservedSelection))
        {
            this.SelectedItem = preservedSelection;
        }

        this.InvalidateRepeatersAndLayout();

        _ = this.DispatcherQueue.TryEnqueue(this.UpdateOverflowButtonVisibility);
    }

    /// <inheritdoc/>
    public void InsertItemAt(int index, object item)
    {
        if (item is not TabItem tabItem)
        {
            throw new ArgumentException("Item must be a TabItem", nameof(item));
        }

        this.Items.Insert(index, tabItem);
    }

    /// <summary>
    ///     Inserts an item and asynchronously waits for the ItemsRepeater to realize its container.
    ///     This implements the Attached→Ready handshake used by the drag coordinator.
    /// </summary>
    public async Task<RealizationResult> InsertItemAsync(int index, object item, CancellationToken cancellationToken, int timeoutMs = 500)
    {
        this.AssertUIThread();

        if (item is not TabItem tabItem)
        {
            throw new ArgumentException("Item must be a TabItem", nameof(item));
        }

        // Create a TCS and register it *before* inserting so we don't miss a very-fast preparation
        var tcs = new TaskCompletionSource<FrameworkElement?>(TaskCreationOptions.RunContinuationsAsynchronously);
        this.pendingRealizations[tabItem.ContentId] = tcs;

        // Insert into the logical collection first (authoritative)
        using (this.EnterExternalInsertScope(index, tabItem.ContentId))
        {
            this.Items.Insert(index, tabItem);
        }

        // Fast-path: the container may already be realized
        var fast = this.GetContainerForIndex(index);
        if (fast is { })
        {
            _ = this.pendingRealizations.Remove(tabItem.ContentId);
            return RealizationResult.Success(fast);
        }

        // Select repeater based on pinned state
        var repeater = tabItem.IsPinned ? this.pinnedItemsRepeater : this.regularItemsRepeater;

        if (repeater is null)
        {
            // No repeater to observe; treat as timeout/failure
            _ = this.pendingRealizations.Remove(tabItem.ContentId);
            return RealizationResult.Failure(RealizationResult.Status.TimedOut);
        }

        using var linked = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        linked.CancelAfter(timeoutMs);

        using var reg = linked.Token.Register(() => tcs.TrySetCanceled());

        var succeeded = false;
        try
        {
            var container = await tcs.Task.ConfigureAwait(false);
            if (container is not null)
            {
                // Container realized — control may animate here if desired
                succeeded = true;
                return RealizationResult.Success(container);
            }

            // Completed with null (shouldn't happen) — treat as timeout
            // Fall through to failure handling which will remove the inserted clone
            return RealizationResult.Failure(RealizationResult.Status.TimedOut);
        }
        catch (TaskCanceledException)
        {
            // Distinguish external cancellation vs timeout
            var status = cancellationToken.IsCancellationRequested
                ? RealizationResult.Status.Cancelled
                : RealizationResult.Status.TimedOut;

            return RealizationResult.Failure(status);
        }
        catch (Exception ex)
        {
            return RealizationResult.Failure(RealizationResult.Status.Error, ex);
        }
        finally
        {
            // Clean up pending waiter first
            _ = this.pendingRealizations.Remove(tabItem.ContentId);

            // If the handshake did not succeed, ensure the inserted clone is removed so the
            // coordinator does not need to perform removal. Locate by ContentId to handle
            // duplicate clones safely and only remove the first matching entry.
            if (!succeeded)
            {
                try
                {
                    var removeIndex = this.Items.IndexOf(tabItem);
                    if (removeIndex >= 0)
                    {
                        try
                        {
                            this.suppressCollectionChangeHandling = true;
                            this.Items.RemoveAt(removeIndex);
                            Debug.WriteLine($"[TabStrip] InsertItemAsync removed pending clone at index={removeIndex} (ContentId={tabItem.ContentId})");
                        }
                        finally
                        {
                            this.suppressCollectionChangeHandling = false;
                        }

                        // Ensure layout and repeaters reflect the removal
                        this.InvalidateRepeatersAndLayout();
                    }
                }
                catch (Exception ex)
                {
                    // Swallow non-fatal exceptions but log for diagnostics
                    Debug.WriteLine($"[TabStrip] InsertItemAsync: failed removing pending clone: {ex}");
                }
            }
        }
    }

    private IDisposable EnterExternalInsertScope(int index, Guid contentId)
    {
        this.pendingExternalInsert = new ExternalInsertInfo(contentId, index);
        return new ExternalInsertScope(this);
    }

    private sealed class ExternalInsertScope : IDisposable
    {
        private readonly TabStrip owner;
        private bool disposed;

        public ExternalInsertScope(TabStrip owner)
        {
            this.owner = owner;
        }

        public void Dispose()
        {
            if (this.disposed)
            {
                return;
            }

            this.owner.pendingExternalInsert = null;
            this.disposed = true;
        }
    }

    /// <inheritdoc/>
    public async Task<ExternalDropPreparationResult?> PrepareExternalDropAsync(
        object payload,
        SpatialPoint<ElementSpace> pointerPosition,
        CancellationToken cancellationToken,
        int timeoutMs = 500)
    {
        this.AssertUIThread();

        ArgumentNullException.ThrowIfNull(payload);

        if (payload is not TabItem tabItem)
        {
            throw new ArgumentException("Payload must be a TabItem.", nameof(payload));
        }

        if (tabItem.IsPinned)
        {
            Debug.WriteLine("[TabStrip] PrepareExternalDropAsync aborted: payload is pinned.");
            return null;
        }

        var snapshots = this.TakeSnapshot();
        var insertionIndex = this.ResolveInsertionIndex(pointerPosition, snapshots);

        try
        {
            var realization = await this.InsertItemAsync(insertionIndex, payload, cancellationToken, timeoutMs).ConfigureAwait(true);
            if (realization.CurrentStatus != RealizationResult.Status.Realized)
            {
                Debug.WriteLine($"[TabStrip] PrepareExternalDropAsync unrealized: status={realization.CurrentStatus}.");
                return null;
            }

            var realizedContainer = this.ResolveRealizedContainer(insertionIndex, realization.Container);
            if (realizedContainer is null)
            {
                Debug.WriteLine("[TabStrip] PrepareExternalDropAsync missing realized container.");
                return null;
            }

            return new ExternalDropPreparationResult(insertionIndex, realizedContainer);
        }
        catch (OperationCanceledException)
        {
            Debug.WriteLine("[TabStrip] PrepareExternalDropAsync cancelled.");
            return null;
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[TabStrip] PrepareExternalDropAsync failed: {ex}");
            return null;
        }
    }

    private int ResolveInsertionIndex(SpatialPoint<ElementSpace> pointerPosition, IReadOnlyList<TabStripItemSnapshot> snapshots)
    {
        var itemsCount = this.Items.Count;
        if (itemsCount == 0)
        {
            return 0;
        }

        if (snapshots.Count == 0)
        {
            return Math.Clamp(this.CountPinnedItems(), 0, itemsCount);
        }

        var pointerX = pointerPosition.Point.X;
        var closest = snapshots
            .Select(snapshot => new
            {
                Snapshot = snapshot,
                Left = snapshot.LayoutOrigin.Point.X + snapshot.Offset,
                Right = snapshot.LayoutOrigin.Point.X + snapshot.Offset + snapshot.Width,
                Center = snapshot.LayoutOrigin.Point.X + snapshot.Offset + (snapshot.Width / 2),
                Distance = Math.Abs(pointerX - (snapshot.LayoutOrigin.Point.X + snapshot.Offset + (snapshot.Width / 2))),
            })
            .OrderBy(x => x.Distance)
            .ThenBy(x => x.Center)
            .First();

        var index = pointerX <= closest.Left
            ? closest.Snapshot.ItemIndex
            : pointerX >= closest.Right
                ? closest.Snapshot.ItemIndex + 1
                : pointerX < closest.Center
                    ? closest.Snapshot.ItemIndex
                    : closest.Snapshot.ItemIndex + 1;

        return Math.Clamp(index, 0, itemsCount);
    }

    private FrameworkElement? ResolveRealizedContainer(int index, FrameworkElement? candidate)
    {
        if (candidate is Grid wrapperGrid)
        {
            var realized = wrapperGrid.Children.OfType<TabStripItem>().FirstOrDefault();
            if (realized is not null)
            {
                return realized;
            }
        }

        return candidate ?? this.GetContainerForIndex(index);
    }

    private int CountPinnedItems()
        => this.Items.TakeWhile(tab => tab.IsPinned).Count();

    /// <inheritdoc/>
    public int HitTestWithThreshold(SpatialPoint<ElementSpace> elementPoint, double threshold)
    {
        // Compute distances from edges, point is relative to strip, so strip X,Y is 0,0
        var p = elementPoint.Point;
        var dxLeft = p.X;
        var dxRight = this.ActualWidth - p.X;
        var dyTop = p.Y;
        var dyBottom = this.ActualHeight - p.Y;

        // Minimum inward distance from all sides
        var minInward = Math.Min(Math.Min(dxLeft, dxRight), Math.Min(dyTop, dyBottom));

        if (minInward > threshold)
        {
            return +1; // inside by more than threshold
        }

        if (minInward < -threshold)
        {
            return -1; // outside by more than threshold
        }

        return 0; // within threshold band
    }

    /// <summary>
    ///     Requests a preview image for the specified tab item during drag operations.
    /// </summary>
    /// <param name="item">The tab item being dragged.</param>
    /// <param name="descriptor">The descriptor to populate with preview image.</param>
    public void RequestPreviewImage(object item, DragVisualDescriptor descriptor)
    {
        ArgumentNullException.ThrowIfNull(descriptor);
        if (item is not TabItem { } tabItem)
        {
            throw new ArgumentException("Item must be a TabItem", nameof(item));
        }

        try
        {
            var eventArgs = new TabDragImageRequestEventArgs
            {
                Item = tabItem,
                RequestedSize = descriptor.RequestedSize,
                PreviewBitmap = null,
            };

            this.TabDragImageRequest?.Invoke(this, eventArgs);

            // If the handler set a preview image, update the descriptor
            if (eventArgs.PreviewBitmap is not null)
            {
                descriptor.PreviewBitmap = eventArgs.PreviewBitmap;
            }
        }
#pragma warning disable CA1031 // Do not catch general exception types
        catch (Exception ex)
        {
            this.LogTabDragImageRequestException(ex);
        }
#pragma warning restore CA1031 // Do not catch general exception types
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

    private void OnPreviewPointerPressed(object sender, PointerRoutedEventArgs e)
    {
        ArgumentNullException.ThrowIfNull(e);
        this.AssertUIThread();

        var point = e.GetCurrentPoint(relativeTo: null).Position.AsScreen();
        var hitItem = (e.OriginalSource as DependencyObject)?.FindAscendant<TabStripItem>();

        if (hitItem is not null)
        {
            this.hotspotOffsets = e.GetCurrentPoint(hitItem).Position;
        }

        Debug.WriteLine($"OnPreviewPointerPressed: hitItem={(hitItem?.Tag ?? "null")}, point={point}, hotspotOffsets={this.hotspotOffsets}");

        _ = this.CapturePointer(e.Pointer);
        this.HandlePointerPressed(hitItem, point);
    }

    /// <summary>
    ///     Handles preview (tunneling) pointer moved events. Tracks pointer movement and initiates
    ///     drag when movement exceeds the threshold.
    /// </summary>
    /// <param name="sender">The event source.</param>
    /// <param name="e">Pointer event arguments.</param>
    /// <remarks>
    ///     This handler uses tunneling (preview) events to track pointer movement continuously,
    ///     even when TabStripItem children would normally handle the events.
    /// </remarks>
    private void OnPreviewPointerMoved(object sender, PointerRoutedEventArgs e)
    {
        ArgumentNullException.ThrowIfNull(e);
        this.AssertUIThread();

        var point = e.GetCurrentPoint(relativeTo: this).Position.AsElement();
        e.Handled = this.HandlePointerMoved(point);
    }

    private void OnPreviewPointerReleased(object sender, PointerRoutedEventArgs e)
    {
        ArgumentNullException.ThrowIfNull(e);
        this.AssertUIThread();

        // Get pointer position in LOGICAL screen coordinates (desktop-relative DIPs)
        var point = e.GetCurrentPoint(relativeTo: null).Position.AsScreen();
        e.Handled = this.HandlePointerReleased(point);

        this.hotspotOffsets = null;
        this.ReleasePointerCapture(e.Pointer);
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
            this.pinnedItemsRepeater.ElementIndexChanged -= this.OnItemsRepeaterElementIndexChanged;
        }

        this.pinnedItemsRepeater = this.GetRequiredTemplatePart<ItemsRepeater>(PartPinnedItemsRepeaterName);
        this.pinnedItemsRepeater.ElementPrepared += this.OnItemsRepeaterElementPrepared;
        this.pinnedItemsRepeater.ElementClearing += this.OnItemsRepeaterElementClearing;
        this.pinnedItemsRepeater.ElementIndexChanged += this.OnItemsRepeaterElementIndexChanged;
        this.pinnedItemsRepeater.ItemsSource = this.PinnedItemsView;
    }

    private void InitializeRegularItemsRepeaterPart()
    {
        if (this.regularItemsRepeater is not null)
        {
            this.regularItemsRepeater.ElementPrepared -= this.OnItemsRepeaterElementPrepared;
            this.regularItemsRepeater.ElementClearing -= this.OnItemsRepeaterElementClearing;
            this.regularItemsRepeater.ElementIndexChanged -= this.OnItemsRepeaterElementIndexChanged;
        }

        this.regularItemsRepeater = this.GetRequiredTemplatePart<ItemsRepeater>(PartRegularItemsRepeaterName);
        this.regularItemsRepeater.ElementPrepared += this.OnItemsRepeaterElementPrepared;
        this.regularItemsRepeater.ElementClearing += this.OnItemsRepeaterElementClearing;
        this.regularItemsRepeater.ElementIndexChanged += this.OnItemsRepeaterElementIndexChanged;
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
        Debug.Assert(args.Element is FrameworkElement { DataContext: TabItem }, "control expects DataContext to be set with a TabItem, on each of its TabStrip items");

        // The element is a Grid wrapper (from ItemTemplate) containing a TabStripItem or placeholder Border
        if (args.Element is not Grid wrapperGrid)
        {
            this.LogBadItem(sender, args.Element);
            return;
        }

        // Find the TabStripItem child within the Grid wrapper
        var tsi = wrapperGrid.Children.OfType<TabStripItem>().FirstOrDefault();
        if (tsi is not { Item: { } ti })
        {
            this.LogBadItem(sender, args.Element);
            return;
        }

        // Cache it in the realized items list
        var index = this.Items.IndexOf(ti);
        var pinned = sender == this.pinnedItemsRepeater;
        this.LogSetupPreparedItem(index, ti, this.TabWidthPolicy == TabWidthPolicy.Compact && !pinned, pinned);

        this.realizedItems.Add(new RealizedItemInfo(wrapperGrid, index, ReferenceEquals(sender, this.pinnedItemsRepeater)));

        // UI-thread: notify any pending InsertItemAsync waiter for this content id
        Debug.Assert(this.DispatcherQueue.HasThreadAccess);
        if (this.pendingRealizations.TryGetValue(ti.ContentId, out var waiter))
        {
            _ = this.pendingRealizations.Remove(ti.ContentId);
            waiter.TrySetResult(wrapperGrid);
        }

        tsi.SetValue(AutomationProperties.NameProperty, ti.Header);
        ti.IsSelected = ReferenceEquals(this.SelectedItem, ti); // exact reference match
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

    private void OnItemsRepeaterElementClearing(ItemsRepeater sender, ItemsRepeaterElementClearingEventArgs args)
    {
        Debug.Assert(args.Element is Grid, "control expects each element to be a Grid wrapper from ItemTemplate");

        // The element is a Grid wrapper; find the TabStripItem child
        if (args.Element is not Grid wrapperGrid)
        {
            this.LogBadItem(sender, args.Element);
            return;
        }

        // Remove from realized items cache
        _ = this.realizedItems.RemoveAll(info => ReferenceEquals(info.Element, wrapperGrid));

        var tsi = wrapperGrid.Children.OfType<TabStripItem>().FirstOrDefault();
        Debug.Assert(tsi is not null, $"Expecting the wrapper grid to have a child of type {nameof(TabStripItem)}");
        if (tsi is null)
        {
            return;
        }

        if (tsi.IsDragging)
        {
            tsi.IsDragging = false;
        }

        this.LogClearElement(tsi);
        tsi.Tapped -= this.OnTabElementTapped;
        tsi.CloseRequested -= this.OnTabCloseRequested;
    }

    private void OnItemsRepeaterElementIndexChanged(ItemsRepeater sender, ItemsRepeaterElementIndexChangedEventArgs args)
    {
        if (args.Element is not Grid wrapperGrid || wrapperGrid.DataContext is not TabItem ti)
        {
            this.LogBadItem(sender, args.Element);
            return;
        }

        this.LogItemIndexChanged(sender, args);
        this.RefreshRealizedItemIndex(wrapperGrid, ti, sender);
    }

    private void RefreshRealizedItemIndex(Grid wrapperGrid, TabItem ti, ItemsRepeater sender)
    {
        var itemsIndex = this.Items.IndexOf(ti);
        for (var i = 0; i < this.realizedItems.Count; i++)
        {
            if (ReferenceEquals(this.realizedItems[i].Element, wrapperGrid))
            {
                this.realizedItems[i] = new RealizedItemInfo(wrapperGrid, itemsIndex, ReferenceEquals(sender, this.pinnedItemsRepeater));
                break;
            }
        }
    }

    private void RefreshAllRealizedItemIndices()
    {
        for (var i = 0; i < this.realizedItems.Count; i++)
        {
            var info = this.realizedItems[i];
            if (info.Element is Grid grid && grid.DataContext is TabItem ti)
            {
                var itemsIndex = this.Items.IndexOf(ti);
                this.realizedItems[i] = new RealizedItemInfo(grid, itemsIndex, info.IsPinned);
            }
        }
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

    private void TabStrip_Unloaded(object? sender, RoutedEventArgs e)
    {
        // Unsubscribe immediately and dispose proxies so we do not hold onto
        // event handlers or dispatcher closures longer than necessary.
        this.Unloaded -= this.TabStrip_Unloaded;

        // Uninstall drag detection handlers
        this.UninstallDragPointerHandlers();

        this.pinnedProxy?.Dispose();
        this.pinnedProxy = null;
        this.regularProxy?.Dispose();
        this.regularProxy = null;

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
                if (wrapper?.DataContext is TabItem { } ti)
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
                if (child is Grid wrapper && wrapper.DataContext is TabItem { } ti)
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
        var items = this.Items;

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

        if (!this.suppressCollectionChangeHandling && items?.Count > 0 && this.SelectedItem is null)
        {
            this.SelectedItem = items[0];
        }

        _ = this.DispatcherQueue.TryEnqueue(this.UpdateOverflowButtonVisibility);
    }

    // Handle additions: compute desired index, log, and defer move/selection to the dispatcher
    private void HandleAddedItems(ObservableCollection<TabItem> items, NotifyCollectionChangedEventArgs e)
    {
        var indexOffset = 0;

        foreach (var newItem in e.NewItems!)
        {
            if (newItem is not TabItem ti)
            {
                indexOffset++;
                continue;
            }

            var count = items.Count;
            this.LogItemAdded(ti, count);

            var resolvedIndex = e.NewStartingIndex >= 0
                ? Math.Clamp(e.NewStartingIndex + indexOffset, 0, items.Count - 1)
                : items.IndexOf(ti);

            if (resolvedIndex < 0)
            {
                indexOffset++;
                continue;
            }

            int? externalTarget = null;
            if (this.pendingExternalInsert is { } external && ti.ContentId == external.ContentId && resolvedIndex == external.TargetIndex)
            {
                externalTarget = external.TargetIndex;
            }

            this.LogTabInsertionPlan(ti, resolvedIndex, externalTarget);

            if (externalTarget is int targetIndex)
            {
                this.DeferMoveAndSelect(items, ti, () => targetIndex);
            }
            else
            {
                var capturedResolvedIndex = resolvedIndex; // Snapshot for deferred evaluation.
                var isExplicit = e.NewStartingIndex >= 0; // remember if caller requested a specific index
                this.DeferMoveAndSelect(items, ti, () => this.ComputeDesiredInsertIndex(items, ti, capturedResolvedIndex, isExplicit));
            }

            indexOffset++;
        }
    }

    // Enqueue move + selection logic so it runs after the CollectionChanged event completes.
    // The Func<int> lets us compute the destination index with the latest selection state and avoid stale snapshots.
    private void DeferMoveAndSelect(ObservableCollection<TabItem> items, TabItem ti, Func<int> desiredIndexProvider)
    {
        ArgumentNullException.ThrowIfNull(desiredIndexProvider);

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
                    var target = desiredIndexProvider();
                    var clampedTarget = Math.Min(Math.Max(0, target), items.Count - 1);
                    var moved = false;
                    if (curIndex >= 0 && curIndex != clampedTarget)
                    {
                        items.Move(curIndex, clampedTarget);
                        this.RefreshAllRealizedItemIndices();
                        moved = true;
                    }

                    this.LogDeferredMoveResult(ti, curIndex, target, clampedTarget, moved);

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
    }

    private int ComputeDesiredInsertIndex(ObservableCollection<TabItem> items, TabItem item, int resolvedIndex, bool isExplicit = false)
    {
        var pinnedCount = this.CountPinnedItems();
        var selectedItem = this.SelectedItem as TabItem;
        var selectedIndex = selectedItem is not null && items.Contains(selectedItem)
            ? items.IndexOf(selectedItem)
            : -1;
        var selectedIsPinned = selectedItem?.IsPinned ?? false;

        // Pinned tabs must stay in the leading bucket, so clamp to the pinned boundary.
        if (item.IsPinned)
        {
            var resultPinned = Math.Min(resolvedIndex, pinnedCount);
            this.LogComputedDesiredIndex(item, resolvedIndex, resolvedIndex, resultPinned, selectedIndex, selectedIsPinned, pinnedCount);
            return resultPinned;
        }

        var anchorIndex = resolvedIndex;

        // For explicit insertions (caller requested a specific index) respect the requested
        // position and do not bias toward the right of the selection. For non-explicit
        // (e.g., appended or generated) inserts, prefer placing regular tabs to the right
        // of the current selection when possible.
        if (!isExplicit && selectedItem is not null && selectedIndex >= 0 && !selectedIsPinned)
        {
            anchorIndex = Math.Max(anchorIndex, selectedIndex + 1);
        }

        // Regular tabs must never intrude into the pinned segment.
        var result = Math.Max(anchorIndex, pinnedCount);
        this.LogComputedDesiredIndex(item, resolvedIndex, anchorIndex, result, selectedIndex, selectedIsPinned, pinnedCount);
        return result;
    }

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
        // If the previously-selected item is gone, choose a deterministic adjacent item.
        // Preferred policy: select the item that occupies the removed item's index (right neighbor),
        // clamped to the last item. This matches common tab-strip behavior and avoids leaving
        // the control without a selection.
        if (this.SelectedItem is TabItem sel && !items.Contains(sel))
        {
            if (items.Count == 0)
            {
                // No items remain
                this.SelectedItem = null;
                this.SelectedIndex = -1;
                return;
            }

            // Use the starting index of the removal. If it's not provided (negative),
            // defensively pick 0 so we still choose a valid neighbor.
            var removedIndex = e.OldStartingIndex >= 0 ? e.OldStartingIndex : 0;

            // Prefer the right neighbor (the item that now occupies the removed index).
            var newIndex = Math.Min(removedIndex, items.Count - 1);
            newIndex = Math.Max(0, newIndex);

            this.SelectedItem = items[newIndex];
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

    private readonly record struct ExternalInsertInfo(Guid ContentId, int TargetIndex);

    private readonly record struct RealizedItemInfo(FrameworkElement Element, int Index, bool IsPinned);
}
