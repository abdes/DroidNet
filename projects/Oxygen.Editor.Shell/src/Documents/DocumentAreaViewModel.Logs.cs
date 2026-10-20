// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
///     Logging methods for <see cref="DocumentAreaViewModel" />.
/// </summary>
public sealed partial class DocumentAreaViewModel
{
    [LoggerMessage(Level = LogLevel.Warning, Message = "The view of the editor that leaves the document area failed to deactivate.")]
    private partial void LogViewDeactivationFailed(Exception exception);

    [LoggerMessage(Level = LogLevel.Information, Message = "'{Title}': {Message}")]
    private partial void LogConflictAction(string title, string message);
}
