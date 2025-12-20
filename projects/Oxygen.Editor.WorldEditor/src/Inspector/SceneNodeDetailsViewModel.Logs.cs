// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// Logging methods for <see cref="SceneNodeDetailsViewModel"/>.
/// </summary>
public sealed partial class SceneNodeDetailsViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Constructed SceneNodeDetailsViewModel. HasDispatcher={HasDispatcher}")]
    private static partial void LogConstructed(ILogger logger, bool HasDispatcher);

    [Conditional("DEBUG")]
    private void LogConstructed(bool hasDispatcher) => LogConstructed(this.logger, hasDispatcher);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Node changed. Old={OldNodeName}, New={NewNodeName}")]
    private static partial void LogNodeChanged(ILogger logger, string? OldNodeName, string? NewNodeName);

    [Conditional("DEBUG")]
    private void LogNodeChanged(string? oldNodeName, string? newNodeName) => LogNodeChanged(this.logger, oldNodeName, newNodeName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Add component requested. TypeId={TypeId}, Node={NodeName}, VmCount={VmCount}, Selected={SelectedComponentName}")]
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
        Message = "ApplyAddComponent. Node={NodeName}, ComponentType={ComponentType}, ComponentName={ComponentName}, NodeComponentsBefore={NodeComponentsBefore}, VmCountBefore={VmCountBefore}")]
    private static partial void LogApplyAddComponent(ILogger logger, string nodeName, string componentType, string componentName, int nodeComponentsBefore, int vmCountBefore);

    [Conditional("DEBUG")]
    private void LogApplyAddComponent(string nodeName, string componentType, string componentName, int nodeComponentsBefore, int vmCountBefore)
        => LogApplyAddComponent(this.logger, nodeName, componentType, componentName, nodeComponentsBefore, vmCountBefore);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "ApplyAddComponent result. Added={Added}, NodeComponentsAfter={NodeComponentsAfter}, VmCountAfter={VmCountAfter}")]
    private static partial void LogApplyAddComponentResult(ILogger logger, bool Added, int NodeComponentsAfter, int VmCountAfter);

    [Conditional("DEBUG")]
    private void LogApplyAddComponentResult(bool added, int nodeComponentsAfter, int vmCountAfter)
        => LogApplyAddComponentResult(this.logger, added, nodeComponentsAfter, vmCountAfter);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "ApplyDeleteComponent. Node={NodeName}, ComponentType={ComponentType}, ComponentName={ComponentName}, NodeComponentsBefore={NodeComponentsBefore}, VmCountBefore={VmCountBefore}, Selected={SelectedComponentName}")]
    private static partial void LogApplyDeleteComponent(ILogger logger, string nodeName, string componentType, string componentName, int nodeComponentsBefore, int vmCountBefore, string? selectedComponentName);

    [Conditional("DEBUG")]
    private void LogApplyDeleteComponent(string nodeName, string componentType, string componentName, int nodeComponentsBefore, int vmCountBefore, string? selectedComponentName)
        => LogApplyDeleteComponent(this.logger, nodeName, componentType, componentName, nodeComponentsBefore, vmCountBefore, selectedComponentName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "ApplyDeleteComponent result. Removed={Removed}, NodeComponentsAfter={NodeComponentsAfter}, VmCountAfter={VmCountAfter}, SelectedAfter={SelectedAfter}")]
    private static partial void LogApplyDeleteComponentResult(ILogger logger, bool Removed, int NodeComponentsAfter, int VmCountAfter, string? SelectedAfter);

    [Conditional("DEBUG")]
    private void LogApplyDeleteComponentResult(bool removed, int nodeComponentsAfter, int vmCountAfter, string? selectedAfter)
        => LogApplyDeleteComponentResult(this.logger, removed, nodeComponentsAfter, vmCountAfter, selectedAfter);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Unexpected exception in {Operation}")]
    private static partial void LogUnexpectedException(ILogger logger, string operation, Exception ex);

    [Conditional("DEBUG")]
    private void LogUnexpectedException(string operation, Exception ex) => LogUnexpectedException(this.logger, operation, ex);
}
