// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.SceneInspector;

/// <summary>
///    Logging methods for <see cref="SceneInspectorViewModel"/>.
/// </summary>
public partial class SceneInspectorViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Constructed SceneInspectorViewModel with {InitialItemCount} items.")]
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
        Message = "SceneInspectorViewModel disposed.")]
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
        Level = LogLevel.Error,
        Message = "Unexpected exception in {Operation}")]
    private static partial void LogUnexpectedException(ILogger logger, string operation, Exception ex);

    [Conditional("DEBUG")]
    private void LogUnexpectedException(string operation, Exception ex) => LogUnexpectedException(this.logger, operation, ex);
}
