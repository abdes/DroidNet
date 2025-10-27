// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

public partial class SceneNodeEditorViewModel
{
    [LoggerMessage(EventId = 3710, Level = LogLevel.Debug, Message = "Constructed SceneNodeEditorViewModel with {InitialItemCount} items.")]
    private static partial void LogConstructed(ILogger logger, int InitialItemCount);

    [LoggerMessage(EventId = 3711, Level = LogLevel.Debug, Message = "SceneNode selection changed. New count: {NewCount}.")]
    private static partial void LogSelectionChanged(ILogger logger, int NewCount);

    [LoggerMessage(EventId = 3712, Level = LogLevel.Debug, Message = "SceneNodeEditorViewModel disposed.")]
    private static partial void LogDisposed(ILogger logger);

    [LoggerMessage(EventId = 3713, Level = LogLevel.Debug, Message = "Filtered property editors: before={Before}, after={After}.")]
    private static partial void LogFiltered(ILogger logger, int Before, int After);

    // Instance wrappers are provided on the ViewModel partial and call the static methods
    // using the protected Logger property exposed by the base class.
    [Conditional("DEBUG")]
    private void LogConstructed(int initialItemCount) => LogConstructed(this.logger, initialItemCount);

    [Conditional("DEBUG")]
    private void LogSelectionChanged(int newCount) => LogSelectionChanged(this.logger, newCount);

    [Conditional("DEBUG")]
    private void LogDisposed() => LogDisposed(this.logger);

    [Conditional("DEBUG")]
    private void LogFiltered(int before, int after) => LogFiltered(this.logger, before, after);
}
