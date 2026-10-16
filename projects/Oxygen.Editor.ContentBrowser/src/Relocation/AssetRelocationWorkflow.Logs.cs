// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ContentBrowser.Relocation;

#pragma warning disable SA1204 // Each log method keeps its static and instance forms together

/// <summary>Log messages.</summary>
public sealed partial class AssetRelocationWorkflow
{
    [LoggerMessage(SkipEnabledCheck = true, Level = LogLevel.Debug, Message = "{Title}: nothing to change.")]
    private static partial void LogNothingToDo(ILogger logger, string title);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogNothingToDo(string title)
        => LogNothingToDo(this.logger, title);

    [LoggerMessage(Level = LogLevel.Information, Message = "{Title} cancelled at the confirmation; {Referrers} files use the content or are open.")]
    private partial void LogReviewCancelled(string title, int referrers);

    [LoggerMessage(Level = LogLevel.Information, Message = "{Action} of '{Label}' requested.")]
    private partial void LogReplayRequested(string action, string label);


    [LoggerMessage(SkipEnabledCheck = true, Level = LogLevel.Debug, Message = "{Title} result shown (succeeded: {Succeeded}): {Message}")]
    private static partial void LogOutcome(ILogger logger, string title, bool succeeded, string message);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogOutcome(string title, bool succeeded, string message)
        => LogOutcome(this.logger, title, succeeded, message);
}
