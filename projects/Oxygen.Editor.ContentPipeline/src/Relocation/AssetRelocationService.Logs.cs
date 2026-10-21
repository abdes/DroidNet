// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>Logging helpers for <see cref="AssetRelocationService"/>.</summary>
/// <remarks>
/// Levels: Information for each requested change and each file moved, rewritten, copied or recycled; Debug for plan
/// details; Warning for a change that did not happen and a file whose references could not be updated; Error for
/// unexpected failures.
/// </remarks>
public sealed partial class AssetRelocationService
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Relocation requested: '{Source}' -> '{Target}'.")]
    private static partial void LogRelocationRequested(ILogger logger, string source, string target);

    private void LogRelocationRequested(string source, string target) => LogRelocationRequested(this.logger, source, target);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Input product rename requested: '{Product}' -> '{Name}'.")]
    private static partial void LogProductRenameRequested(ILogger logger, string product, string name);

    private void LogProductRenameRequested(string product, string name) => LogProductRenameRequested(this.logger, product, name);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "'{Label}' recorded in the history; undoing it runs the reverse relocation.")]
    private static partial void LogRecorded(ILogger logger, string label);

    private void LogRecorded(string label) => LogRecorded(this.logger, label);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "{Action} of '{Label}' requested.")]
    private static partial void LogReplayRequested(ILogger logger, string action, string label);

    private void LogReplayRequested(string action, string label) => LogReplayRequested(this.logger, action, label);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Relocation changes nothing; no file is touched.")]
    private static partial void LogNothingToRelocate(ILogger logger);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogNothingToRelocate()
        => LogNothingToRelocate(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Relocation planned: {Moves} moves, {Edits} rewritten files, {Referrers} referrers.")]
    private static partial void LogRelocationPlanned(ILogger logger, int moves, int edits, int referrers);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogRelocationPlanned(int moves, int edits, int referrers)
        => LogRelocationPlanned(this.logger, moves, edits, referrers);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Relocation committed: {Moves} moves and {Edits} rewritten files.")]
    private static partial void LogRelocationCommitted(ILogger logger, int moves, int edits);

    private void LogRelocationCommitted(int moves, int edits) => LogRelocationCommitted(this.logger, moves, edits);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "'{Path}' could not be read; references in it are not updated.")]
    private static partial void LogUnreadableFile(ILogger logger, string path);

    private void LogUnreadableFile(string path) => LogUnreadableFile(this.logger, path);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "{Operation} not done: {Reason}")]
    private static partial void LogNotDone(ILogger logger, string operation, string reason);

    private void LogNotDone(string operation, string reason) => LogNotDone(this.logger, operation, reason);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "{Operation} failed unexpectedly.")]
    private static partial void LogFailed(ILogger logger, Exception exception, string operation);

    private void LogFailed(Exception exception, string operation) => LogFailed(this.logger, exception, operation);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Copied '{Source}' to '{Target}'.")]
    private static partial void LogCopied(ILogger logger, string source, string target);

    private void LogCopied(string source, string target) => LogCopied(this.logger, source, target);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Copy failed after creating {Created} files and folders; removing them.")]
    private static partial void LogCopyFailed(ILogger logger, Exception exception, int created);

    private void LogCopyFailed(Exception exception, int created) => LogCopyFailed(this.logger, exception, created);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Could not remove '{Path}' after the failed copy; delete it by hand.")]
    private static partial void LogCopyCleanupFailed(ILogger logger, Exception exception, string path);

    private void LogCopyCleanupFailed(Exception exception, string path) => LogCopyCleanupFailed(this.logger, exception, path);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Moving '{Path}' to the Recycle Bin.")]
    private static partial void LogRecycling(ILogger logger, string path);

    private void LogRecycling(string path) => LogRecycling(this.logger, path);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Moved {Count} items to the Recycle Bin.")]
    private static partial void LogRecycled(ILogger logger, int count);

    private void LogRecycled(int count) => LogRecycled(this.logger, count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Notifying {Participants} open-document participants of the committed relocation ({Rewritten} rewritten files).")]
    private static partial void LogNotifyingParticipants(ILogger logger, int participants, int rewritten);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogNotifyingParticipants(int participants, int rewritten)
        => LogNotifyingParticipants(this.logger, participants, rewritten);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Open document participant {Participant} could not follow the relocation; its next save reports the file conflict.")]
    private static partial void LogParticipantFailed(ILogger logger, Exception exception, string participant);

    private void LogParticipantFailed(Exception exception, IAssetRelocationParticipant participant)
    {
        if (!this.logger.IsEnabled(LogLevel.Error))
        {
            return;
        }

        var name = participant.GetType().Name;
        LogParticipantFailed(this.logger, exception, name);
    }
}
