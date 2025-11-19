// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using CommunityToolkit.WinUI;
using DroidNet.Coordinates;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Windows.Foundation;

namespace DroidNet.Aura.Controls;

/// <summary>
///     A lightweight, reusable tab strip control for WinUI 3 that displays a dynamic row of tabs
///     and raises events or executes commands when tabs are invoked, selected, or closed.
/// </summary>
public partial class TabStrip
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Applying template...")]
    private static partial void LogApplyingTemplate(ILogger logger);

    private void LogApplyingTemplate()
    {
        if (this.logger is ILogger logger)
        {
            LogApplyingTemplate(logger);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Template part '{PartName}' (required={IsRequired}) of type '{ExpectedType}' not found.")]
    private static partial void LogTemplatePartNotFound(ILogger logger, string partName, string expectedType, bool isRequired);

    private void LogTemplatePartNotFound(string partName, Type expectedType, bool isRequired)
    {
        if (this.logger is ILogger logger)
        {
            LogTemplatePartNotFound(logger, partName, expectedType.Name, isRequired);
        }
    }

    // Logs pointer entered on a tab item
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Pointer entered tab item: {TabHeader}")]
    private static partial void LogPointerEntered(ILogger logger, string? TabHeader);

    [Conditional("DEBUG")]
    private void LogPointerEntered(object? sender)
    {
        if (this.logger is ILogger logger && sender is FrameworkElement fe && fe.DataContext is TabItem ti)
        {
            LogPointerEntered(logger, ti.Header);
        }
    }

    // Logs pointer exited on a tab item
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Pointer exited tab item: {TabHeader}")]
    private static partial void LogPointerExited(ILogger logger, string? TabHeader);

    [Conditional("DEBUG")]
    private void LogPointerExited(object? sender)
    {
        if (this.logger is ILogger logger && sender is FrameworkElement fe && fe.DataContext is TabItem ti)
        {
            LogPointerExited(logger, ti.Header);
        }
    }

    // Logs tab invoked
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Tab invoked: Header='{Header}', Index={Index}")]
    private static partial void LogTabInvoked(ILogger logger, string? Header, int Index);

    [Conditional("DEBUG")]
    private void LogTabInvoked(TabItem? item)
    {
        if (this.logger is ILogger logger && item is not null)
        {
            LogTabInvoked(logger, item.Header, this.SelectedIndex);
        }
    }

    // Logs selection changed
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Tab selection changed: OldHeader='{OldHeader}', NewHeader='{NewHeader}', OldIndex={OldIndex}, NewIndex={NewIndex}")]
    private static partial void LogSelectionChanged(ILogger logger, string? OldHeader, string? NewHeader, int OldIndex, int NewIndex);

    private void LogSelectionChanged(TabItem? oldItem, TabItem? newItem, int oldIndex, int newIndex)
    {
        if (this.logger is ILogger logger)
        {
            LogSelectionChanged(logger, oldItem?.Header, newItem?.Header, oldIndex, newIndex);
        }
    }

    // Logs overflow left button click
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Overflow left button clicked")]
    private static partial void LogOverflowLeftClicked(ILogger logger);

    [Conditional("DEBUG")]
    private void LogOverflowLeftClicked()
    {
        if (this.logger is ILogger logger)
        {
            LogOverflowLeftClicked(logger);
        }
    }

    // Logs overflow right button click
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Overflow right button clicked")]
    private static partial void LogOverflowRightClicked(ILogger logger);

    [Conditional("DEBUG")]
    private void LogOverflowRightClicked()
    {
        if (this.logger is ILogger logger)
        {
            LogOverflowRightClicked(logger);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Other methods should follow the pattern of this pair of sample logging methods ({Foo}.")]
    private static partial void LogSample(ILogger logger, string foo);

    [Conditional("DEBUG")]
    private void LogSample(float oldValue, string foo)
    {
        if (this.logger is ILogger logger)
        {
            LogSample(logger, foo);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Tab item added: Header='{Header}', IsPinned={IsPinned}, IsClosable={IsClosable}, NewCount={Count}")]
    private static partial void LogItemAdded(ILogger logger, string Header, bool IsPinned, bool IsClosable, int Count);

    private void LogItemAdded(TabItem? item, int count)
    {
        if (this.logger is ILogger logger && item is not null)
        {
            LogItemAdded(logger, item.Header ?? "<null>", item.IsPinned, item.IsClosable, count);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Tab insertion plan: Header='{Header}', IsPinned={IsPinned}, ResolvedIndex={ResolvedIndex}, ExternalTarget={ExternalTarget}, SelectedIndex={SelectedIndex}, PinnedCount={PinnedCount}")]
    private static partial void LogTabInsertionPlan(ILogger logger, string Header, bool IsPinned, int ResolvedIndex, string ExternalTarget, int SelectedIndex, int PinnedCount);

    private void LogTabInsertionPlan(TabItem item, int resolvedIndex, int? externalTarget)
    {
        if (this.logger is ILogger logger)
        {
            var externalText = externalTarget.HasValue
                ? externalTarget.Value.ToString(CultureInfo.InvariantCulture)
                : "<none>";
            var selectedIndex = this.SelectedItem is TabItem selected && this.Items.Contains(selected)
                ? this.Items.IndexOf(selected)
                : -1;
            var pinnedCount = this.CountPinnedItems();
            LogTabInsertionPlan(logger, item.Header ?? "<null>", item.IsPinned, resolvedIndex, externalText, selectedIndex, pinnedCount);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Deferred move evaluated: Header='{Header}', CurrentIndex={CurrentIndex}, DesiredIndex={DesiredIndex}, ClampedIndex={ClampedIndex}, Moved={Moved}")]
    private static partial void LogDeferredMoveResult(ILogger logger, string Header, int CurrentIndex, int DesiredIndex, int ClampedIndex, bool Moved);

    private void LogDeferredMoveResult(TabItem item, int currentIndex, int desiredIndex, int clampedIndex, bool moved)
    {
        if (this.logger is ILogger logger)
        {
            LogDeferredMoveResult(logger, item.Header ?? "<null>", currentIndex, desiredIndex, clampedIndex, moved);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Computed desired index: Header='{Header}', ResolvedIndex={ResolvedIndex}, AnchorIndex={AnchorIndex}, Result={Result}, SelectedIndex={SelectedIndex}, SelectedIsPinned={SelectedIsPinned}, PinnedCount={PinnedCount}")]
    private static partial void LogComputedDesiredIndex(ILogger logger, string Header, int ResolvedIndex, int AnchorIndex, int Result, int SelectedIndex, bool SelectedIsPinned, int PinnedCount);

    private void LogComputedDesiredIndex(TabItem item, int resolvedIndex, int anchorIndex, int result, int selectedIndex, bool selectedIsPinned, int pinnedCount)
    {
        if (this.logger is ILogger logger)
        {
            LogComputedDesiredIndex(logger, item.Header ?? "<null>", resolvedIndex, anchorIndex, result, selectedIndex, selectedIsPinned, pinnedCount);
        }
    }

    // Logs template part not found
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Template part not found: {PartName}")]
    private static partial void LogTemplatePartNotFound(ILogger logger, string PartName);

    private void LogTemplatePartNotFound(string partName)
    {
        if (this.logger is ILogger logger)
        {
            LogTemplatePartNotFound(logger, partName);
        }
    }

    // Logs tab sizing applied
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Tab sizing applied: Policy={Policy}, CalculatedWidth={CalculatedWidth}, MaxWidth={MaxWidth}, TabCount={TabCount}, AvailableWidth={AvailableWidth}")]
    private static partial void LogTabSizing(ILogger logger, TabWidthPolicy Policy, double? CalculatedWidth, double MaxWidth, int TabCount, double AvailableWidth);

    [Conditional("DEBUG")]
    private void LogTabSizing(TabWidthPolicy policy, double? calculatedWidth, double maxWidth, int tabCount, double availableWidth)
    {
        if (this.logger is ILogger logger)
        {
            LogTabSizing(logger, policy, calculatedWidth, maxWidth, tabCount, availableWidth);
        }
    }

    // Logs size changed
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "TabStrip size changed: NewWidth={NewWidth}, NewHeight={NewHeight}")]
    private static partial void LogSizeChanged(ILogger logger, double NewWidth, double NewHeight);

    [Conditional("DEBUG")]
    private void LogSizeChanged(double newWidth, double newHeight)
    {
        if (this.logger is ILogger logger)
        {
            LogSizeChanged(logger, newWidth, newHeight);
        }
    }

    // Logs unloaded
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "TabStrip unloaded")]
    private static partial void LogUnloaded(ILogger logger);

    [Conditional("DEBUG")]
    private void LogUnloaded()
    {
        if (this.logger is ILogger logger)
        {
            LogUnloaded(logger);
        }
    }

    // Logs bad item prepared
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Element prepared is not a valid TabStripItem in {RepeaterType} repeater: ElementType={ElementType}, DataContextType={DataContextType}")]
    private static partial void LogBadItem(ILogger logger, string RepeaterType, string ElementType, string DataContextType);

    private void LogBadItem(ItemsRepeater sender, object element)
    {
        if (this.logger is ILogger logger)
        {
            var elementType = element?.GetType().Name ?? "null";
            var dataContextType = (element as FrameworkElement)?.DataContext?.GetType().Name ?? "null";
            var repeaterType = sender == this.pinnedItemsRepeater ? "pinned" : "regular";
            LogBadItem(logger, repeaterType, elementType, dataContextType);
        }
    }

    // Logs setup prepared item
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Setting up prepared Items[{index}]: Header='{Header}', IsPinned={IsPinned}, Compacting={Compacting}, Bucket={Bucket}")]
    private static partial void LogSetupPreparedItem(ILogger logger, int index, string Header, bool IsPinned, bool Compacting, string Bucket);

    [Conditional("DEBUG")]
    private void LogSetupPreparedItem(int index, TabItem ti, bool compacting, bool pinned)
    {
        if (this.logger is ILogger logger)
        {
            LogSetupPreparedItem(logger, index, ti.Header ?? "<null>", ti.IsPinned, compacting, pinned ? "pinned" : "regular");
        }
    }

    // Logs clearing element
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Clearing element: Header='{Header}'")]
    private static partial void LogClearElement(ILogger logger, string Header);

    [Conditional("DEBUG")]
    private void LogClearElement(TabStripItem tsi)
    {
        if (this.logger is ILogger logger)
        {
            var header = (tsi.DataContext as TabItem)?.Header ?? "<null>";
            LogClearElement(logger, header);
        }
    }

    // Logs clearing element
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Repeater({Repeater}) item index changed: Item='{Item}', OldIndex={OldIndex}, NewIndex='{NewIndex}'")]
    private static partial void LogItemIndexChanged(ILogger logger, string repeater, string item, int oldIndex, int newIndex);

    [Conditional("DEBUG")]
    private void LogItemIndexChanged(ItemsRepeater sender, ItemsRepeaterElementIndexChangedEventArgs args)
    {
        if (this.logger is ILogger logger)
        {
            var tsi = (args.Element as Grid)?.Children.OfType<TabStripItem>().FirstOrDefault();
            var repeaterType = sender == this.pinnedItemsRepeater ? "pinned" : "regular";

            var header = tsi?.Item?.Header ?? "<null>";
            LogItemIndexChanged(logger, repeaterType, header, args.OldIndex, args.NewIndex);
        }
    }

    // Logs when the TabWidthPolicy changes
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "TabWidthPolicy changed: {Policy}")]
    private static partial void LogTabWidthPolicyChanged(ILogger logger, TabWidthPolicy Policy);

    private void LogTabWidthPolicyChanged(TabWidthPolicy policy)
    {
        if (this.logger is ILogger logger)
        {
            LogTabWidthPolicyChanged(logger, policy);
        }
    }

    // Recompute lifecycle logs
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Recomputing tab widths...")]
    private static partial void LogRecomputeStart(ILogger logger);

    [Conditional("DEBUG")]
    private void LogRecomputeStart()
    {
        if (this.logger is ILogger logger)
        {
            LogRecomputeStart(logger);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Recompute found no prepared containers")]
    private static partial void LogRecomputeNoContainers(ILogger logger);

    [Conditional("DEBUG")]
    private void LogRecomputeNoContainers()
    {
        if (this.logger is ILogger logger)
        {
            LogRecomputeNoContainers(logger);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Recompute completed")]
    private static partial void LogRecomputeCompleted(ILogger logger);

    [Conditional("DEBUG")]
    private void LogRecomputeCompleted()
    {
        if (this.logger is ILogger logger)
        {
            LogRecomputeCompleted(logger);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Collected prepared containers: {Count}")]
    private static partial void LogCollectPreparedContainers(ILogger logger, int Count);

    [Conditional("DEBUG")]
    private void LogCollectPreparedContainers(int count)
    {
        if (this.logger is ILogger logger)
        {
            LogCollectPreparedContainers(logger, count);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "MeasureDesiredWidths: policy not Compact, skipping")]
    private static partial void LogMeasurePolicyNotCompact(ILogger logger);

    [Conditional("DEBUG")]
    private void LogMeasurePolicyNotCompact()
    {
        if (this.logger is ILogger logger)
        {
            LogMeasurePolicyNotCompact(logger);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Measured desired widths for {Count} items")]
    private static partial void LogMeasuredDesiredWidths(ILogger logger, int Count);

    [Conditional("DEBUG")]
    private void LogMeasuredDesiredWidths(int count)
    {
        if (this.logger is ILogger logger)
        {
            LogMeasuredDesiredWidths(logger, count);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Built layout inputs: {Count} items")]
    private static partial void LogBuildLayoutInputs(ILogger logger, int Count);

    [Conditional("DEBUG")]
    private void LogBuildLayoutInputs(int count)
    {
        if (this.logger is ILogger logger)
        {
            LogBuildLayoutInputs(logger, count);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "ComputeLayout: availableWidth={AvailableWidth}, inputs={InputCount}")]
    private static partial void LogComputeLayout(ILogger logger, double AvailableWidth, int InputCount);

    [Conditional("DEBUG")]
    private void LogComputeLayout(double availableWidth, int inputCount)
    {
        if (this.logger is ILogger logger)
        {
            LogComputeLayout(logger, availableWidth, inputCount);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "No prepared container found for index {Index}")]
    private static partial void LogNoContainerForIndex(ILogger logger, int Index);

    private void LogNoContainerForIndex(int index)
    {
        if (this.logger is ILogger logger)
        {
            LogNoContainerForIndex(logger, index);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Exception in TabDragImageRequest handler.")]
    private static partial void LogTabDragImageRequestException(ILogger logger, Exception Exception);

    [Conditional("DEBUG")]
    private void LogTabDragImageRequestException(Exception exception)
    {
        if (this.logger is ILogger logger)
        {
            LogTabDragImageRequestException(logger, exception);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Items collection reset: Count={Count}, SelectionCleared={SelectionCleared}")]
    private static partial void LogItemsReset(ILogger logger, int Count, bool SelectionCleared);

    private void LogItemsReset(int count, bool selectionCleared)
    {
        if (this.logger is ILogger logger)
        {
            LogItemsReset(logger, count, selectionCleared);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Realized Element[{Index}]: {Tag}")]
    private static partial void LogRealizedElement(ILogger logger, int Index, string Tag);

    [Conditional("DEBUG")]
    private void LogRealizedElement(int index, string tag)
    {
        if (this.logger is ILogger logger)
        {
            LogRealizedElement(logger, index, tag);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Snapshot[{ItemIndex}]: LayoutOrigin={LayoutOrigin}, Width={Width}")]
    private static partial void LogSnapshot(ILogger logger, int ItemIndex, string LayoutOrigin, double Width);

    [Conditional("DEBUG")]
    private void LogSnapshot(int itemIndex, string layoutOrigin, double width)
    {
        if (this.logger is ILogger logger)
        {
            LogSnapshot(logger, itemIndex, layoutOrigin, width);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "ApplyTransform skipped: contentId={ContentId}, offset={Offset}, reason=container not found (may be unrealized)")]
    private static partial void LogApplyTransformSkipped(ILogger logger, Guid ContentId, double Offset);

    [Conditional("DEBUG")]
    private void LogApplyTransformSkipped(Guid contentId, double offset)
    {
        if (this.logger is ILogger logger)
        {
            LogApplyTransformSkipped(logger, contentId, offset);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "ApplyTransform start: itemIndex={ItemIndex}, offset={Offset}, realizedCount={RealizedCount}")]
    private static partial void LogApplyTransformStart(ILogger logger, int ItemIndex, double Offset, int RealizedCount);

    [Conditional("DEBUG")]
    private void LogApplyTransformStart(int itemIndex, double offset, int realizedCount)
    {
        if (this.logger is ILogger logger)
        {
            LogApplyTransformStart(logger, itemIndex, offset, realizedCount);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "ApplyTransform skip: realized element at index {Index} is not Grid")]
    private static partial void LogApplyTransformSkip(ILogger logger, int Index);

    [Conditional("DEBUG")]
    private void LogApplyTransformSkip(int index)
    {
        if (this.logger is ILogger logger)
        {
            LogApplyTransformSkip(logger, index);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "ApplyTransform created new TranslateTransform for itemIndex={ItemIndex}")]
    private static partial void LogApplyTransformCreated(ILogger logger, int ItemIndex);

    [Conditional("DEBUG")]
    private void LogApplyTransformCreated(int itemIndex)
    {
        if (this.logger is ILogger logger)
        {
            LogApplyTransformCreated(logger, itemIndex);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Transform(X={Offset}) applied to Item[contentId={ContentId}] '{ItemName}'.")]
    private static partial void LogTransform(ILogger logger, Guid ContentId, string itemName, double Offset);

    [Conditional("DEBUG")]
    private void LogTransform(Guid contentId, FrameworkElement element, double offset)
    {
        if (this.logger is ILogger logger)
        {
            var tsi = element.FindDescendant<TabStripItem>();
            LogTransform(logger, contentId, tsi?.Item?.Title ?? "<unknown>", offset);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "ApplyTransform end: itemIndex={ItemIndex}")]
    private static partial void LogApplyTransformEnd(ILogger logger, int ItemIndex);

    [Conditional("DEBUG")]
    private void LogApplyTransformEnd(int itemIndex)
    {
        if (this.logger is ILogger logger)
        {
            LogApplyTransformEnd(logger, itemIndex);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "InsertItemAsync removed pending clone at index={Index} (ContentId={ContentId})")]
    private static partial void LogInsertItemAsyncRemoved(ILogger logger, int Index, Guid ContentId);

    [Conditional("DEBUG")]
    private void LogInsertItemAsyncRemoved(int index, Guid contentId)
    {
        if (this.logger is ILogger logger)
        {
            LogInsertItemAsyncRemoved(logger, index, contentId);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "InsertItemAsync: failed removing pending clone")]
    private static partial void LogInsertItemAsyncFailed(ILogger logger, Exception ex);

    [Conditional("DEBUG")]
    private void LogInsertItemAsyncFailed(Exception ex)
    {
        if (this.logger is ILogger logger)
        {
            LogInsertItemAsyncFailed(logger, ex);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "PrepareExternalDropAsync aborted: payload is pinned.")]
    private static partial void LogPrepareExternalDropAborted(ILogger logger);

    private void LogPrepareExternalDropAborted()
    {
        if (this.logger is ILogger logger)
        {
            LogPrepareExternalDropAborted(logger);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "PrepareExternalDropAsync unrealized: status={Status}.")]
    private static partial void LogPrepareExternalDropUnrealized(ILogger logger, string Status);

    private void LogPrepareExternalDropUnrealized(string status)
    {
        if (this.logger is ILogger logger)
        {
            LogPrepareExternalDropUnrealized(logger, status);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "PrepareExternalDropAsync missing realized container.")]
    private static partial void LogPrepareExternalDropMissingContainer(ILogger logger);

    private void LogPrepareExternalDropMissingContainer()
    {
        if (this.logger is ILogger logger)
        {
            LogPrepareExternalDropMissingContainer(logger);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "PrepareExternalDropAsync cancelled.")]
    private static partial void LogPrepareExternalDropCancelled(ILogger logger);

    private void LogPrepareExternalDropCancelled()
    {
        if (this.logger is ILogger logger)
        {
            LogPrepareExternalDropCancelled(logger);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "PrepareExternalDropAsync failed")]
    private static partial void LogPrepareExternalDropFailed(ILogger logger, Exception ex);

    private void LogPrepareExternalDropFailed(Exception ex)
    {
        if (this.logger is ILogger logger)
        {
            LogPrepareExternalDropFailed(logger, ex);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Coordinator subscribed.")]
    private static partial void LogCoordinatorSubscribed(ILogger logger);

    [Conditional("DEBUG")]
    private void LogCoordinatorSubscribed()
    {
        if (this.logger is ILogger logger)
        {
            LogCoordinatorSubscribed(logger);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Coordinator unsubscribed.")]
    private static partial void LogCoordinatorUnsubscribed(ILogger logger);

    [Conditional("DEBUG")]
    private void LogCoordinatorUnsubscribed()
    {
        if (this.logger is ILogger logger)
        {
            LogCoordinatorUnsubscribed(logger);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Pointer pressed on item '{HitItem}' at Position={Position}, HotSpot={HotspotOffsets}).")]
    private static partial void LogPointerPressedImpl(ILogger logger, string hitItem, SpatialPoint<ElementSpace> position, Point hotspotOffsets);

    [Conditional("DEBUG")]
    private void LogPointerPressed(TabStripItem item, SpatialPoint<ElementSpace> position, Point hotspotOffsets)
    {
        if (this.logger is ILogger logger)
        {
            LogPointerPressedImpl(logger, item.Item?.Title ?? "<unknown>", position, hotspotOffsets);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Pointer moved to ({X}, {Y}), delta={Delta}.")]
    private static partial void LogPointerMovedImpl(ILogger logger, double X, double Y, double Delta);

    [Conditional("DEBUG")]
    private void LogPointerMoved(TabStripItem item, Point position, double delta)
    {
        if (this.logger is ILogger logger)
        {
            LogPointerMovedImpl(logger, position.X, position.Y, delta);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Pointer released while drag is ongoing.")]
    private static partial void LogPointerReleasedWhileDragging(ILogger logger);

    [Conditional("DEBUG")]
    private void LogPointerReleasedWhileDragging()
    {
        if (this.logger is ILogger logger)
        {
            LogPointerReleasedWhileDragging(logger);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Drag threshold exceeded: delta={Delta} >= threshold={Threshold}.")]
    private static partial void LogThresholdExceeded(ILogger logger, double Delta, double Threshold);

    [Conditional("DEBUG")]
    private void LogThresholdExceeded(double delta, double threshold)
    {
        if (this.logger is ILogger logger)
        {
            LogThresholdExceeded(logger, delta, threshold);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Initiating drag for item '{Item}' inside TabStrip '{Strip}' at screen point ({Point})")]
    private static partial void LogBeginDragStartedImpl(ILogger logger, string item, string strip, SpatialPoint<ElementSpace> point);

    [Conditional("DEBUG")]
    private void LogInitiateDrag(TabItem item, SpatialPoint<ElementSpace> point)
    {
        if (this.logger is ILogger logger)
        {
            LogBeginDragStartedImpl(logger, item.Title, this.Name, point);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Selection cleared for drag.")]
    private static partial void LogSelectionClearedImpl(ILogger logger);

    [Conditional("DEBUG")]
    private void LogSelectionCleared()
    {
        if (this.logger is ILogger logger)
        {
            LogSelectionClearedImpl(logger);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to initiate drag.")]
    private static partial void LogDragSessionFailure(ILogger logger, InvalidOperationException Exception);

    [Conditional("DEBUG")]
    private void LogInitiateDragFailed(InvalidOperationException exception)
    {
        if (this.logger is ILogger logger)
        {
            LogDragSessionFailure(logger, exception);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Coordinator reported drag moved to ({X}, {Y}).")]
    private static partial void LogCoordinatorDragMovedImpl(ILogger logger, double X, double Y);

    [Conditional("DEBUG")]
    private void LogCoordinatorDragMoved(Point screenPoint)
    {
        if (this.logger is ILogger logger)
        {
            LogCoordinatorDragMovedImpl(logger, screenPoint.X, screenPoint.Y);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Coordinator reported drag ended at ({X}, {Y}), destination hash={DestHash}, newIndex={NewIndex}.")]
    private static partial void LogCoordinatorDragEndedImpl(ILogger logger, double X, double Y, int DestHash, int NewIndex);

    [Conditional("DEBUG")]
    private void LogCoordinatorDragEnded(Point screenPoint, TabStrip? destination, int? newIndex)
    {
        if (this.logger is ILogger logger)
        {
            LogCoordinatorDragEndedImpl(logger, screenPoint.X, screenPoint.Y, destination?.GetHashCode() ?? 0, newIndex ?? -1);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Exception during drag end.")]
    private static partial void LogDragEndException(ILogger logger, Exception Exception);

    [Conditional("DEBUG")]
    private void LogDragEndException(Exception exception)
    {
        if (this.logger is ILogger logger)
        {
            LogDragEndException(logger, exception);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Exception in TabCloseRequested handler.")]
    private static partial void LogTabCloseRequestedException(ILogger logger, Exception Exception);

    [Conditional("DEBUG")]
    private void LogTabCloseRequestedException(Exception exception)
    {
        if (this.logger is ILogger logger)
        {
            LogTabCloseRequestedException(logger, exception);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Exception in TabDragComplete handler.")]
    private static partial void LogTabDragCompleteException(ILogger logger, Exception Exception);

    [Conditional("DEBUG")]
    private void LogTabDragCompleteException(Exception exception)
    {
        if (this.logger is ILogger logger)
        {
            LogTabDragCompleteException(logger, exception);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Exception in TabTearOutRequested handler.")]
    private static partial void LogTabTearOutRequestedException(ILogger logger, Exception Exception);

    [Conditional("DEBUG")]
    private void LogTabTearOutRequestedException(Exception exception)
    {
        if (this.logger is ILogger logger)
        {
            LogTabTearOutRequestedException(logger, exception);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Exception in TabDetachRequested handler.")]
    private static partial void LogTabDetachRequestedException(ILogger logger, Exception Exception);

    private void LogTabDetachRequestedException(Exception exception)
    {
        if (this.logger is ILogger logger)
        {
            LogTabDetachRequestedException(logger, exception);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Drag state cleanup for '{Header}' (ContentId={ContentId}): cleared {ClearedCount} container(s).")]
    private static partial void LogDragStateCleanupImpl(ILogger logger, string Header, Guid ContentId, int ClearedCount);

    [Conditional("DEBUG")]
    private void LogDragStateCleanup(TabItem item, int clearedCount)
    {
        if (this.logger is ILogger logger)
        {
            LogDragStateCleanupImpl(logger, item.Header ?? "<null>", item.ContentId, clearedCount);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "TryCompleteDrag: failed clearing IsDragging for item (ContentId={ContentId})")]
    private static partial void LogTryCompleteDragFailed(ILogger logger, Guid ContentId, Exception ex);

    private void LogTryCompleteDragFailed(Guid contentId, Exception ex)
    {
        if (this.logger is ILogger logger)
        {
            LogTryCompleteDragFailed(logger, contentId, ex);
        }
    }
}
