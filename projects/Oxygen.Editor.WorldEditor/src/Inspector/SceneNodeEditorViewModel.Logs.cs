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
}
