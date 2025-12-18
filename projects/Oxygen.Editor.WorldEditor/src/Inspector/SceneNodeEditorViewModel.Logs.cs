// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.WorldEditor.Inspector;

/// <summary>
///    Logging methods for <see cref="SceneNodeEditorViewModel"/>.
/// </summary>
public partial class SceneNodeEditorViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Constructed SceneNodeEditorViewModel with {InitialItemCount} items.")]
    private static partial void LogConstructed(ILogger logger, int InitialItemCount);

    [Conditional("DEBUG")]
    private void LogConstructed(int initialItemCount) => LogConstructed(this.logger, initialItemCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Selection changed. New item count: {NewCount}.")]
    private static partial void LogSelectionChanged(ILogger logger, int NewCount);

    [Conditional("DEBUG")]
    private void LogSelectionChanged(int newCount) => LogSelectionChanged(this.logger, newCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "SceneNodeEditorViewModel disposed.")]
    private static partial void LogDisposed(ILogger logger);

    [Conditional("DEBUG")]
    private void LogDisposed() => LogDisposed(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Filtered properties. Before: {Before}, After: {After}.")]
    private static partial void LogFiltered(ILogger logger, int Before, int After);

    [Conditional("DEBUG")]
    private void LogFiltered(int before, int after) => LogFiltered(this.logger, before, after);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Add component requested. TypeId={TypeId}, Node={NodeName}, ComponentsVmCount={VmCount}, SelectedComponent={SelectedComponentName}")]
    private static partial void LogAddComponentRequested(ILogger logger, string typeId, string nodeName, int vmCount, string? selectedComponentName);

    [Conditional("DEBUG")]
    private void LogAddComponentRequested(string typeId, string nodeName, int vmCount, string? selectedComponentName)
        => LogAddComponentRequested(this.logger, typeId, nodeName, vmCount, selectedComponentName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Add component failed: unknown TypeId={TypeId}")]
    private static partial void LogAddComponentFactoryFailed(ILogger logger, string typeId);

    [Conditional("DEBUG")]
    private void LogAddComponentFactoryFailed(string typeId) => LogAddComponentFactoryFailed(this.logger, typeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "ApplyAddComponent. Node={NodeName}, ComponentType={ComponentType}, ComponentName={ComponentName}, NodeComponentsBefore={NodeComponentsBefore}")]
    private static partial void LogApplyAddComponent(ILogger logger, string nodeName, string componentType, string componentName, int nodeComponentsBefore);

    [Conditional("DEBUG")]
    private void LogApplyAddComponent(string nodeName, string componentType, string componentName, int nodeComponentsBefore)
        => LogApplyAddComponent(this.logger, nodeName, componentType, componentName, nodeComponentsBefore);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "ApplyDeleteComponent. Node={NodeName}, ComponentType={ComponentType}, ComponentName={ComponentName}, NodeComponentsBefore={NodeComponentsBefore}, SelectedComponent={SelectedComponentName}")]
    private static partial void LogApplyDeleteComponent(ILogger logger, string nodeName, string componentType, string componentName, int nodeComponentsBefore, string? selectedComponentName);

    [Conditional("DEBUG")]
    private void LogApplyDeleteComponent(string nodeName, string componentType, string componentName, int nodeComponentsBefore, string? selectedComponentName)
        => LogApplyDeleteComponent(this.logger, nodeName, componentType, componentName, nodeComponentsBefore, selectedComponentName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Components rebuild. Reason={Reason}, Node={NodeName}, VmCount={VmCount}, PrevSelected={PrevSelected}, NowSelected={NowSelected}")]
    private static partial void LogComponentsRebuilt(ILogger logger, string reason, string? nodeName, int vmCount, string? prevSelected, string? nowSelected);

    [Conditional("DEBUG")]
    private void LogComponentsRebuilt(string reason, string? nodeName, int vmCount, string? prevSelected, string? nowSelected)
        => LogComponentsRebuilt(this.logger, reason, nodeName, vmCount, prevSelected, nowSelected);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Components refresh skipped. Reason={Reason}, IsSingle={IsSingle}, SelectedNodes={SelectedNodes}, TargetNode={TargetNode}")]
    private static partial void LogComponentsRefreshSkipped(ILogger logger, string reason, bool isSingle, int selectedNodes, string targetNode);

    [Conditional("DEBUG")]
    private void LogComponentsRefreshSkipped(string reason, bool isSingle, int selectedNodes, string targetNode)
        => LogComponentsRefreshSkipped(this.logger, reason, isSingle, selectedNodes, targetNode);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Components update enqueued. Reason={Reason}, Node={NodeName}, Component={ComponentName}")]
    private static partial void LogComponentsUpdateEnqueued(ILogger logger, string reason, string nodeName, string componentName);

    [Conditional("DEBUG")]
    private void LogComponentsUpdateEnqueued(string reason, string nodeName, string componentName)
        => LogComponentsUpdateEnqueued(this.logger, reason, nodeName, componentName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Unexpected exception in {Operation}")]
    private static partial void LogUnexpectedException(ILogger logger, string operation, Exception ex);

    [Conditional("DEBUG")]
    private void LogUnexpectedException(string operation, Exception ex) => LogUnexpectedException(this.logger, operation, ex);
}
