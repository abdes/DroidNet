// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

public abstract partial class MultiSelectionDetails<T>
    where T : GameObject
{
    // LoggerMessage-generated logging helpers
    [LoggerMessage(
        EventId = 3601,
        Level = LogLevel.Trace,
        Message = "[MultiSelectionDetails: `{ItemsType}`] Items collection updated (Count={Count})")]
    private static partial void LogItemsCollectionUpdated(ILogger logger, string itemsType, int count);

    [LoggerMessage(
        EventId = 3602,
        Level = LogLevel.Trace,
        Message = "[MultiSelectionDetails: `{ItemsType}`] Refreshed properties (HasItems={HasItems}, ItemsCount={Count}, NameMixed={NameMixed})")]
    private static partial void LogRefreshedProperties(ILogger logger, string itemsType, bool hasItems, int count, string? nameMixed);

    [LoggerMessage(
        EventId = 3603,
        Level = LogLevel.Trace,
        Message = "[MultiSelectionDetails: `{ItemsType}`] Property editors updated (EditorsCount={EditorsCount})")]
    private static partial void LogPropertyEditorsUpdated(ILogger logger, string itemsType, int editorsCount);

    [LoggerMessage(
        EventId = 3604,
        Level = LogLevel.Trace,
        Message = "[MultiSelectionDetails: `{ItemsType}`] Property editor values updated (EditorsCount={EditorsCount})")]
    private static partial void LogPropertyEditorsValuesUpdated(ILogger logger, string itemsType, int editorsCount);

    [LoggerMessage(
        EventId = 3605,
        Level = LogLevel.Trace,
        Message = "[MultiSelectionDetails: `{ItemsType}`] Name propagated to items (NewName={NewName}, ItemsCount={ItemsCount})")]
    private static partial void LogNamePropagated(ILogger logger, string itemsType, string? newName, int itemsCount);

    // Instance wrappers for easier usage in the class
    private void LogItemsCollectionUpdated() => LogItemsCollectionUpdated(this.logger, typeof(T).Name ?? "<Unknown>", this.items.Count);
    private void LogRefreshedProperties(string? nameMixed) => LogRefreshedProperties(this.logger, typeof(T).Name ?? "<Unknown>", this.HasItems, this.ItemsCount, nameMixed);
    private void LogPropertyEditorsUpdated() => LogPropertyEditorsUpdated(this.logger, typeof(T).Name ?? "<Unknown>", this.PropertyEditors?.Count ?? 0);
    private void LogPropertyEditorsValuesUpdated() => LogPropertyEditorsValuesUpdated(this.logger, typeof(T).Name ?? "<Unknown>", this.PropertyEditors?.Count ?? 0);
    private void LogNamePropagated(string? newName) => LogNamePropagated(this.logger, typeof(T).Name ?? "<Unknown>", newName, this.ItemsCount);
}
