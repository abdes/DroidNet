// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
///     Logging helpers for <see cref="DocumentAreaViewModel" />.
/// </summary>
public sealed partial class DocumentAreaViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The view of the editor that leaves the document area failed to deactivate.")]
    private static partial void LogViewDeactivationFailed(ILogger logger, Exception exception);

    private void LogViewDeactivationFailed(Exception exception) => LogViewDeactivationFailed(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "'{Title}': {Message}")]
    private static partial void LogConflictAction(ILogger logger, string title, string message);

    private void LogConflictAction(string title, string message) => LogConflictAction(this.logger, title, message);
}
