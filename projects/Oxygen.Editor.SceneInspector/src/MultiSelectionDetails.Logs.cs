// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Logging helpers for <see cref="MultiSelectionDetails{T}"/>.</summary>
public abstract partial class MultiSelectionDetails<T>
{
    [LoggerMessage(
        EventId = 3601,
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "[MultiSelectionDetails: `{ItemsType}`] Items collection updated (Count={Count})")]
    private static partial void LogItemsCollectionUpdated(ILogger logger, string itemsType, int count);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogItemsCollectionUpdated()
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        var itemsType = typeof(T).Name ?? "<Unknown>";
        LogItemsCollectionUpdated(this.logger, itemsType, this.items.Count);
    }

    [LoggerMessage(
        EventId = 3602,
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "[MultiSelectionDetails: `{ItemsType}`] Refreshed properties (HasItems={HasItems}, ItemsCount={Count}, NameMixed={NameMixed})")]
    private static partial void LogRefreshedProperties(ILogger logger, string itemsType, bool hasItems, int count, string? nameMixed);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogRefreshedProperties(string? nameMixed)
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        var itemsType = typeof(T).Name ?? "<Unknown>";
        LogRefreshedProperties(this.logger, itemsType, this.HasItems, this.ItemsCount, nameMixed);
    }

    [LoggerMessage(
        EventId = 3603,
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "[MultiSelectionDetails: `{ItemsType}`] Property editors updated (EditorsCount={EditorsCount})")]
    private static partial void LogPropertyEditorsUpdated(ILogger logger, string itemsType, int editorsCount);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogPropertyEditorsUpdated()
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        var itemsType = typeof(T).Name ?? "<Unknown>";
        var editorsCount = this.PropertyEditors?.Count ?? 0;
        LogPropertyEditorsUpdated(this.logger, itemsType, editorsCount);
    }

    [LoggerMessage(
        EventId = 3604,
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "[MultiSelectionDetails: `{ItemsType}`] Property editor values updated (EditorsCount={EditorsCount})")]
    private static partial void LogPropertyEditorsValuesUpdated(ILogger logger, string itemsType, int editorsCount);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogPropertyEditorsValuesUpdated()
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        var itemsType = typeof(T).Name ?? "<Unknown>";
        var editorsCount = this.PropertyEditors?.Count ?? 0;
        LogPropertyEditorsValuesUpdated(this.logger, itemsType, editorsCount);
    }

    [LoggerMessage(
        EventId = 3605,
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "[MultiSelectionDetails: `{ItemsType}`] Name propagated to items (NewName={NewName}, ItemsCount={ItemsCount})")]
    private static partial void LogNamePropagated(ILogger logger, string itemsType, string? newName, int itemsCount);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogNamePropagated(string? newName)
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        var itemsType = typeof(T).Name ?? "<Unknown>";
        LogNamePropagated(this.logger, itemsType, newName, this.ItemsCount);
    }
}
