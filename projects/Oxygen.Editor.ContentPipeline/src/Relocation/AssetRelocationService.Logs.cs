// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ContentPipeline.Relocation;

#pragma warning disable SA1204 // Static elements should appear before instance elements

/// <inheritdoc cref="AssetRelocationService"/>
/// <remarks>
/// Levels: Information for each requested change and each file moved, rewritten, copied or recycled; Debug for plan
/// details; Warning for a change that did not happen, a file whose references could not be updated, and a cook that
/// did not publish; Error for unexpected failures.
/// </remarks>
public sealed partial class AssetRelocationService
{
    [LoggerMessage(Level = LogLevel.Information, Message = "Relocation requested: '{Source}' -> '{Target}'.")]
    private partial void LogRelocationRequested(string source, string target);

    [LoggerMessage(Level = LogLevel.Information, Message = "Output group relocation requested: model '{Model}' -> group '{Group}'.")]
    private partial void LogGroupRelocationRequested(string model, string group);

    [LoggerMessage(SkipEnabledCheck = true, Level = LogLevel.Debug, Message = "Relocation changes nothing; no file is touched.")]
    private static partial void LogNothingToRelocate(ILogger logger);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogNothingToRelocate()
        => LogNothingToRelocate(this.logger);

    [LoggerMessage(SkipEnabledCheck = true, Level = LogLevel.Debug, Message = "Relocation planned: {Moves} moves, {Edits} rewritten files, {Referrers} referrers, {GroupFolders} output group folders.")]
    private static partial void LogRelocationPlanned(ILogger logger, int moves, int edits, int referrers, int groupFolders);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogRelocationPlanned(int moves, int edits, int referrers, int groupFolders)
        => LogRelocationPlanned(this.logger, moves, edits, referrers, groupFolders);

    [LoggerMessage(Level = LogLevel.Information, Message = "Relocation committed: {Moves} moves and {Edits} rewritten files.")]
    private partial void LogRelocationCommitted(int moves, int edits);

    [LoggerMessage(Level = LogLevel.Warning, Message = "'{Path}' could not be read; references in it are not updated.")]
    private partial void LogUnreadableFile(string path);

    [LoggerMessage(Level = LogLevel.Warning, Message = "{Operation} not done: {Reason}")]
    private partial void LogNotDone(string operation, string reason);

    [LoggerMessage(Level = LogLevel.Error, Message = "{Operation} failed unexpectedly.")]
    private partial void LogFailed(Exception exception, string operation);

    [LoggerMessage(Level = LogLevel.Information, Message = "Copied '{Source}' to '{Target}'.")]
    private partial void LogCopied(string source, string target);

    [LoggerMessage(Level = LogLevel.Error, Message = "Copy failed after creating {Created} files and folders; removing them.")]
    private partial void LogCopyFailed(Exception exception, int created);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Could not remove '{Path}' after the failed copy; delete it by hand.")]
    private partial void LogCopyCleanupFailed(Exception exception, string path);

    [LoggerMessage(Level = LogLevel.Information, Message = "Moving '{Path}' to the Recycle Bin.")]
    private partial void LogRecycling(string path);

    [LoggerMessage(Level = LogLevel.Information, Message = "Moved {Count} items to the Recycle Bin.")]
    private partial void LogRecycled(int count);

    [LoggerMessage(SkipEnabledCheck = true, Level = LogLevel.Debug, Message = "Notifying {Participants} open-document participants of the committed relocation ({Rewritten} rewritten files).")]
    private static partial void LogNotifyingParticipants(ILogger logger, int participants, int rewritten);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogNotifyingParticipants(int participants, int rewritten)
        => LogNotifyingParticipants(this.logger, participants, rewritten);

    [LoggerMessage(Level = LogLevel.Error, Message = "Open document participant {Participant} could not follow the relocation; its next save reports the file conflict.")]
    private partial void LogParticipantFailed(Exception exception, string participant);

    [LoggerMessage(Level = LogLevel.Information, Message = "Cooking the project after the {Operation} to publish the changed assets and retire the old ones.")]
    private partial void LogCookStarted(string operation);

    [LoggerMessage(Level = LogLevel.Information, Message = "The cook after the {Operation} published ({Status}).")]
    private partial void LogCookPublished(string operation, Oxygen.Managed.Core.Diagnostics.OperationStatus status);

    [LoggerMessage(Level = LogLevel.Warning, Message = "The cook after the {Operation} ended {Status}; the old cooked assets may still be listed: {Diagnostics}")]
    private partial void LogCookNotSucceeded(string operation, Oxygen.Managed.Core.Diagnostics.OperationStatus status, string diagnostics);

    [LoggerMessage(Level = LogLevel.Warning, Message = "The cook after the {Operation} was cancelled; the old cooked assets may still be listed.")]
    private partial void LogCookCancelled(string operation);

    [LoggerMessage(Level = LogLevel.Error, Message = "The cook after the {Operation} failed; the old cooked assets may still be listed.")]
    private partial void LogCookFailed(Exception exception, string operation);
}
