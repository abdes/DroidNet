// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentBrowser.Messages;

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

    [LoggerMessage(Level = LogLevel.Information, Message = "{Title} cancelled at the confirmation; {Referrers} files use the content.")]
    private partial void LogReviewCancelled(string title, int referrers);

    [LoggerMessage(Level = LogLevel.Information, Message = "Undo of the last rename or move requested.")]
    private partial void LogUndoRequested();

    [LoggerMessage(SkipEnabledCheck = true, Level = LogLevel.Debug, Message = "Undo of the earlier rename or move is no longer offered: another change followed it.")]
    private static partial void LogUndoForgotten(ILogger logger);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogUndoForgotten()
        => LogUndoForgotten(this.logger);

    [LoggerMessage(Level = LogLevel.Information, Message = "Asking the scene workspace to {Command} scene '{Scene}' (new name: '{NewName}').")]
    private partial void LogSceneCommand(SceneAssetCommand command, string scene, string? newName);

    [LoggerMessage(Level = LogLevel.Warning, Message = "No world editor handled {Command} of scene '{Scene}'.")]
    private partial void LogSceneCommandUnhandled(SceneAssetCommand command, string scene);

    [LoggerMessage(Level = LogLevel.Error, Message = "{Command} of scene '{Scene}' failed.")]
    private partial void LogSceneCommandFailed(Exception exception, SceneAssetCommand command, string scene);

    [LoggerMessage(SkipEnabledCheck = true, Level = LogLevel.Debug, Message = "{Title} result shown (succeeded: {Succeeded}): {Message}")]
    private static partial void LogOutcome(ILogger logger, string title, bool succeeded, string message);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogOutcome(string title, bool succeeded, string message)
        => LogOutcome(this.logger, title, succeeded, message);

    [LoggerMessage(SkipEnabledCheck = true, Level = LogLevel.Debug, Message = "After {Title}: refreshing the browser and announcing {Moves} moves, {Rewritten} rewritten and {Deleted} deleted files.")]
    private static partial void LogAnnouncingChanges(ILogger logger, string title, int moves, int rewritten, int deleted);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogAnnouncingChanges(string title, int moves, int rewritten, int deleted)
        => LogAnnouncingChanges(this.logger, title, moves, rewritten, deleted);

    [LoggerMessage(Level = LogLevel.Information, Message = "Cooking the project after {Title} to publish the changed scenes.")]
    private partial void LogCookStarted(string title);

    [LoggerMessage(Level = LogLevel.Information, Message = "The cook after {Title} published ({Status}).")]
    private partial void LogCookPublished(string title, Oxygen.Managed.Core.Diagnostics.OperationStatus status);

    [LoggerMessage(Level = LogLevel.Warning, Message = "The cook after {Title} ended {Status}: {Diagnostics}")]
    private partial void LogCookNotSucceeded(string title, Oxygen.Managed.Core.Diagnostics.OperationStatus status, string diagnostics);

    [LoggerMessage(Level = LogLevel.Warning, Message = "The cook after {Title} was cancelled.")]
    private partial void LogCookCancelled(string title);

    [LoggerMessage(Level = LogLevel.Error, Message = "The cook after {Title} failed.")]
    private partial void LogCookFailed(Exception exception, string title);
}
