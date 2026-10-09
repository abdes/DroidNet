// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text.Json;
using DroidNet.Storage;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ContentPipeline.Relocation;

#pragma warning disable SA1204 // Each log method keeps its static and instance forms together

/// <summary>Log messages.</summary>
internal static partial class AssetRelocationTransaction
{
    [System.Diagnostics.Conditional("DEBUG")]
    [LoggerMessage(SkipEnabledCheck = true, Level = LogLevel.Debug, Message = "Relocation journal '{Journal}' opened: {Moves} moves, {Edits} rewrites.")]
    private static partial void LogJournalOpened(ILogger logger, string journal, int moves, int edits);

    [LoggerMessage(Level = LogLevel.Information, Message = "Moved '{Source}' to '{Target}'.")]
    private static partial void LogMoved(ILogger logger, string source, string target);

    [LoggerMessage(Level = LogLevel.Information, Message = "Rewrote references in '{Path}'.")]
    private static partial void LogRewrote(ILogger logger, string path);

    [System.Diagnostics.Conditional("DEBUG")]
    [LoggerMessage(SkipEnabledCheck = true, Level = LogLevel.Debug, Message = "Relocation journal '{Journal}' committed.")]
    private static partial void LogCommitted(ILogger logger, string journal);

    [LoggerMessage(Level = LogLevel.Error, Message = "A relocation step failed; restoring every file recorded in '{Journal}'.")]
    private static partial void LogRestoring(ILogger logger, Exception exception, string journal);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Found interrupted relocation journal '{Journal}'; restoring the project.")]
    private static partial void LogRecovering(ILogger logger, string journal);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Restored the previous content of '{Path}'.")]
    private static partial void LogRestoredFile(ILogger logger, string path);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Moved '{Target}' back to '{Source}'.")]
    private static partial void LogMovedBack(ILogger logger, string target, string source);

    [LoggerMessage(Level = LogLevel.Error, Message = "Could not restore a file recorded in '{Journal}'; the journal is kept and the restore retries when the project opens.")]
    private static partial void LogRestoreFailed(ILogger logger, Exception exception, string journal);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Relocation journal '{Journal}' rolled back; every file was restored.")]
    private static partial void LogRestored(ILogger logger, string journal);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Relocation journal '{Journal}' is unreadable and never recorded a started relocation; deleting it.")]
    private static partial void LogUnreadableJournal(ILogger logger, Exception exception, string journal);

    [System.Diagnostics.Conditional("DEBUG")]
    [LoggerMessage(SkipEnabledCheck = true, Level = LogLevel.Debug, Message = "Could not delete the settled relocation journal '{Journal}'; recovery removes it later.")]
    private static partial void LogJournalNotDeleted(ILogger logger, Exception exception, string journal);
}
