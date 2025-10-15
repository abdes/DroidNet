// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using System.Reflection;
using System.Runtime.CompilerServices;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;

namespace DroidNet.Controls.Menus;

#pragma warning disable SA1204 // Static elements should appear before instance elements

/// <summary>
///     Manages user interactions and navigation flow for hierarchical menu systems.
/// </summary>
public partial class MenuInteractionController
{
    [LoggerMessage(
        EventId = 3401,
        Level = LogLevel.Trace,
        Message = "[MenuInteractionController] navigation source changed to `{NavMode}`")]
    private static partial void LogNavigationModeChanged(ILogger logger, MenuNavigationMode navMode);

    [Conditional("DEBUG")]
    private void LogNavigationModeChanged()
        => LogNavigationModeChanged(services.InteractionLogger, this.NavigationMode);

    [LoggerMessage(
        EventId = 3402,
        Level = LogLevel.Debug,
        Message = "[MenuInteractionController] HoverStarted on item `{ItemId}` (HasChildren: {HasChildren}, IsExpanded: {IsExpanded}), (menuIsExpanded: {MenuIsExpanded}), {@Context}")]
    private static partial void LogHoverStarted(ILogger logger, string itemId, bool hasChildren, bool isExpanded, bool menuIsExpanded, object context);

    [Conditional("DEBUG")]
    private void LogHoverStarted(MenuItemData menuItem, bool menuIsExpanded, MenuInteractionContext context)
        => LogHoverStarted(services.InteractionLogger, menuItem.Id, menuItem.HasChildren, menuItem.IsExpanded, menuIsExpanded, this.SerializeContext(context));

    [LoggerMessage(
        EventId = 3403,
        Level = LogLevel.Debug,
        Message = "[MenuInteractionController] HoverEnded on item `{ItemId}` (IsExpanded: {IsExpanded}), {@Context}")]
    private static partial void LogHoverEnded(ILogger logger, string itemId, bool isExpanded, object context);

    [Conditional("DEBUG")]
    private void LogHoverEnded(MenuItemData menuItem, MenuInteractionContext context)
        => LogHoverEnded(services.InteractionLogger, menuItem.Id, menuItem.IsExpanded, this.SerializeContext(context));

    [LoggerMessage(
        EventId = 3404,
        Level = LogLevel.Debug,
        Message = "[MenuInteractionController] ItemGotFocus item `{ItemId}` source={Source}, {@Context}")]
    private static partial void LogItemGotFocus(ILogger logger, string itemId, MenuInteractionInputSource Source, object context);

    [Conditional("DEBUG")]
    private void LogItemGotFocus(MenuItemData menuItem, MenuInteractionInputSource source, MenuInteractionContext context)
        => LogItemGotFocus(services.InteractionLogger, menuItem.Id, source, this.SerializeContext(context));

    [LoggerMessage(
        EventId = 3405,
        Level = LogLevel.Debug,
        Message = "[MenuInteractionController] ItemLostFocus on item `{ItemId}` (IsExpanded: {IsExpanded}), {@Context}")]
    private static partial void LogItemLostFocus(ILogger logger, string itemId, bool isExpanded, object context);

    [Conditional("DEBUG")]
    private void LogItemLostFocus(MenuItemData menuItem, MenuInteractionContext context)
        => LogItemLostFocus(services.InteractionLogger, menuItem.Id, menuItem.IsExpanded, this.SerializeContext(context));

    [LoggerMessage(
        EventId = 3406,
        Level = LogLevel.Debug,
        Message = "[MenuInteractionController] ExpandRequested item `{ItemId}` source={Source}, {@Context}")]
    private static partial void LogItemExpandRequested(ILogger logger, string itemId, MenuInteractionInputSource Source, object context);

    [Conditional("DEBUG")]
    private void LogItemExpandRequested(MenuItemData menuItem, MenuInteractionInputSource source, MenuInteractionContext context)
        => LogItemExpandRequested(services.InteractionLogger, menuItem.Id, source, this.SerializeContext(context));

    [LoggerMessage(
        EventId = 3407,
        Level = LogLevel.Debug,
        Message = "[MenuInteractionController] Navigate to {Direction} item `{AdjacentItemId}`, {@Context}")]
    private static partial void LogNavigateToAdjacentItem(ILogger logger, MenuNavigationDirection direction, string adjacentItemId, object context);

    [Conditional("DEBUG")]
    private void LogNavigateToAdjacentItem(MenuNavigationDirection direction, string adjacentItemId, MenuInteractionContext context)
        => LogNavigateToAdjacentItem(services.InteractionLogger, direction, adjacentItemId, this.SerializeContext(context));

    [LoggerMessage(
        EventId = 3408,
        Level = LogLevel.Information,
        Message = "[MenuInteractionController] ItemInvoked item `{ItemId}` source={Source}, {@Context}")]
    private static partial void LogItemInvoked(ILogger logger, string itemId, MenuInteractionInputSource Source, object context);

    private void LogItemInvoked(MenuItemData menuItem, MenuInteractionInputSource source, MenuInteractionContext context)
        => LogItemInvoked(services.InteractionLogger, menuItem.Id, source, this.SerializeContext(context));

    [LoggerMessage(
        EventId = 3409,
        Level = LogLevel.Debug,
        Message = "[MenuInteractionController] Navigate to parent column ({ParentLevel}, `{ParentActiveItemId}`), {@Context}")]
    private static partial void LogNavigateToParentColumn(ILogger logger, int parentLevel, string parentActiveItemId, object context);

    [Conditional("DEBUG")]
    private void LogNavigateToParentColumn(int parentLevel, string parentActiveItemId, MenuInteractionContext context)
        => LogNavigateToParentColumn(services.InteractionLogger, parentLevel, parentActiveItemId, this.SerializeContext(context));

    private object SerializeContext(MenuInteractionContext context) => new
    {
        this.NavigationMode,
        context.Kind,
        context.ColumnLevel,
        HasRootSurface = context.RootSurface is not null,
        HasColumnSurface = context.ColumnSurface is not null,
    };

    [LoggerMessage(
        EventId = 3410,
        Level = LogLevel.Debug,
        Message = "[MenuInteractionController] Retrying FocusFirstItem for level={Level} after initial failure")]
    private static partial void LogRetryFocusFirstItem(ILogger logger, int level);

    [Conditional("DEBUG")]
    private void LogRetryFocusFirstItem(int level)
        => LogRetryFocusFirstItem(services.InteractionLogger, level);

    [LoggerMessage(
        EventId = 3411,
        Level = LogLevel.Debug,
        Message = "[MenuInteractionController] DismissRequested context={Kind} column={Column} kind={KindValue}")]
    private static partial void LogDismissRequested(ILogger logger, MenuInteractionContextKind kind, int column, MenuDismissKind kindValue);

    [Conditional("DEBUG")]
    private void LogDismissRequested(MenuInteractionContext context, MenuDismissKind kind)
        => LogDismissRequested(services.MiscLogger, context.Kind, context.ColumnLevel, kind);

    [LoggerMessage(
        EventId = 3412,
        Level = LogLevel.Debug,
        Message = "[MenuInteractionController] Column dismiss scheduling pending dismissal (anchor={Anchor})")]
    private static partial void LogColumnDismissScheduling(ILogger logger, string anchor);

    [Conditional("DEBUG")]
    private void LogColumnDismissScheduling(MenuItemData? anchor)
        => LogColumnDismissScheduling(services.MiscLogger, anchor is null ? "<none>" : anchor.Id);

    [LoggerMessage(
        EventId = 3413,
        Level = LogLevel.Debug,
        Message = "[MenuInteractionController] Dismiss request ignored (no matching surface)")]
    private static partial void LogDismissRequestIgnored(ILogger logger);

    [Conditional("DEBUG")]
    private void LogDismissRequestIgnored()
        => LogDismissRequestIgnored(services.MiscLogger);

    [LoggerMessage(
        EventId = 3414,
        Level = LogLevel.Debug,
        Message = "[MenuInteractionController] ExecuteRequestSubmenu opening submenu for {ItemId}")]
    private static partial void LogExecuteRequestSubmenuOpening(ILogger logger, string itemId);

    [Conditional("DEBUG")]
    private void LogExecuteRequestSubmenuOpening(MenuItemData item)
        => LogExecuteRequestSubmenuOpening(services.MiscLogger, item.Id);

    [LoggerMessage(
        EventId = 3415,
        Level = LogLevel.Debug,
        Message = "[MenuInteractionController] ExpandItem called for column={ColumnLevel} item={ItemId} mode={Mode}; requesting FocusFirstItem for level={TargetLevel}")]
    private static partial void LogExpandItemRequestingFocusFirst(ILogger logger, int columnLevel, string itemId, MenuNavigationMode mode, int targetLevel);

    [Conditional("DEBUG")]
    private void LogExpandItemRequestingFocusFirst(int columnLevel, string itemId, MenuNavigationMode mode, int targetLevel)
        => LogExpandItemRequestingFocusFirst(services.MiscLogger, columnLevel, itemId, mode, targetLevel);

    [LoggerMessage(
        EventId = 3416,
        Level = LogLevel.Debug,
        Message = "[MenuInteractionController] FocusFirstItem result for new column level={Level}: {Result}")]
    private static partial void LogFocusFirstItemResult(ILogger logger, int level, bool result);

    [Conditional("DEBUG")]
    private void LogFocusFirstItemResult(int level, bool result)
        => LogFocusFirstItemResult(services.MiscLogger, level, result);

    [LoggerMessage(
        EventId = 3417,
        Level = LogLevel.Debug,
        Message = "[MenuInteractionController] OnDismissed context={Kind} column={Column}")]
    private static partial void LogOnDismissed(ILogger logger, MenuInteractionContextKind kind, int column);

    [Conditional("DEBUG")]
    private void LogOnDismissed(MenuInteractionContext context)
        => LogOnDismissed(services.MiscLogger, context.Kind, context.ColumnLevel);

    [LoggerMessage(
        EventId = 3418,
        Level = LogLevel.Debug,
        Message = "[MenuInteractionController] Column dismissal focus restored to anchor {Anchor}")]
    private static partial void LogColumnDismissalFocusRestored(ILogger logger, string anchor);

    [Conditional("DEBUG")]
    private void LogColumnDismissalFocusRestored(MenuItemData anchor)
        => LogColumnDismissalFocusRestored(services.MiscLogger, anchor.Id);

    [LoggerMessage(
        EventId = 3419,
        Level = LogLevel.Debug,
        Message = "[MenuInteractionController] Column dismissal without anchor; restoring focus owner")]
    private static partial void LogColumnDismissalWithoutAnchor(ILogger logger);

    [Conditional("DEBUG")]
    private void LogColumnDismissalWithoutAnchor()
        => LogColumnDismissalWithoutAnchor(services.MiscLogger);

    [LoggerMessage(
        EventId = 3420,
        Level = LogLevel.Debug,
        Message = "[MenuInteractionController] Root dismissal without pending entry; restoring focus owner")]
    private static partial void LogRootDismissalWithoutPending(ILogger logger);

    [Conditional("DEBUG")]
    private void LogRootDismissalWithoutPending()
        => LogRootDismissalWithoutPending(services.MiscLogger);

    [LoggerMessage(
        EventId = 3421,
        Level = LogLevel.Warning,
        Message = "[MenuInteractionController] Unable to capture focus owner (root surface not focusable)")]
    private static partial void LogUnableToCaptureNotFocusable(ILogger logger);

    private void LogUnableToCaptureNotFocusable()
        => LogUnableToCaptureNotFocusable(services.FocusLogger);

    [LoggerMessage(
        EventId = 3422,
        Level = LogLevel.Information,
        Message = "[MenuInteractionController] Captured focus owner for dismissal return: {Target}")]
    private static partial void LogCapturedFocusOwner(ILogger logger, string target);

    private void LogCapturedFocusOwner()
    {
        // Compute a compact identifier for the captured focus element (if any).
        // Format: Type('Name')#<shortHash> or <none>/<stale> when not available.
        string ident;

        if (this.focusReturnTarget is not { } weakRef)
        {
            ident = "<none>";
        }
        else if (!weakRef.TryGetTarget(out var element) || element is null)
        {
            ident = "<stale>";
        }
        else
        {
            var name = string.Empty;

            try
            {
                // Try to read a friendly Name property if present (avoids hard dependency on specific UI types).
                var nameProp = element.GetType().GetProperty("Name");
                if (nameProp is not null)
                {
                    var val = nameProp.GetValue(element) as string;
                    if (!string.IsNullOrEmpty(val))
                    {
                        name = val;
                    }
                }
            }
#pragma warning disable CA1031 // Do not catch general exception types
            catch (Exception)
            {
                _ = 0; // Ignore exceptions when accessing the Name property.
            }
#pragma warning restore CA1031 // Do not catch general exception types

            var typeName = element.GetType().FullName ?? element.GetType().Name;
            var shortId = RuntimeHelpers.GetHashCode(element).ToString("x", CultureInfo.InvariantCulture);

            ident = string.IsNullOrEmpty(name)
                ? $"{typeName}#{shortId}"
                : $"{typeName}('{name}')#{shortId}";
        }

        LogCapturedFocusOwner(services.FocusLogger, ident);
    }

    [LoggerMessage(
        EventId = 3423,
        Level = LogLevel.Debug,
        Message = "[MenuInteractionController] Focus owner capture skipped (current focus is a menu item)")]
    private static partial void LogFocusCaptureSkippedMenuItem(ILogger logger);

    [Conditional("DEBUG")]
    private void LogFocusCaptureSkippedMenuItem()
        => LogFocusCaptureSkippedMenuItem(services.FocusLogger);

    [LoggerMessage(
        EventId = 3424,
        Level = LogLevel.Warning,
        Message = "[MenuInteractionController] RestoreFocusOwner failed; clearing target")]
    private static partial void LogRestoreFocusOwnerFailed(ILogger logger);

    private void LogRestoreFocusOwnerFailed()
        => LogRestoreFocusOwnerFailed(services.FocusLogger);

    [LoggerMessage(
        EventId = 3425,
        Level = LogLevel.Warning,
        Message = "[MenuInteractionController] RestoreFocusOwner failed: no captured element")]
    private static partial void LogRestoreFocusNoElement(ILogger logger);

    private void LogRestoreFocusNoElement()
        => LogRestoreFocusNoElement(services.FocusLogger);

    [LoggerMessage(
        EventId = 3426,
        Level = LogLevel.Information,
        Message = "[MenuInteractionController] Focus restore succeeded")]
    private static partial void LogFocusRestoreSucceeded(ILogger logger);

    private void LogFocusRestoreSucceeded()
        => LogFocusRestoreSucceeded(services.FocusLogger);

    [LoggerMessage(
        EventId = 3427,
        Level = LogLevel.Warning,
        Message = "[MenuInteractionController] Focus restore failed")]
    private static partial void LogFocusRestoreFailed(ILogger logger);

    private void LogFocusRestoreFailed()
        => LogFocusRestoreFailed(services.FocusLogger);

    [LoggerMessage(
        EventId = 3428,
        Level = LogLevel.Error,
        Message = "[MenuInteractionController] Expansion of item `{ItemId}` failed, {@Context}")]
    private static partial void LogExpansionError(ILogger logger, MenuInteractionContext context, string itemId, Exception exception);

    private void LogExpansionError(MenuInteractionContext context, MenuItemData menuItemData, Exception exception)
        => LogExpansionError(services.FocusLogger, context, menuItemData.Id, exception);
}
