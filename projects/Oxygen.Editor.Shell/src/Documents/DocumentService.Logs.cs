// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
///     Logging helpers for <see cref="DocumentService"/>.
/// </summary>
public sealed partial class DocumentService
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "'{Asset}' was not opened: {Problem}")]
    private static partial void LogNotOpened(ILogger logger, AssetPath asset, string problem);

    private void LogNotOpened(AssetPath asset, string problem) => LogNotOpened(this.logger, asset, problem);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The editor of '{Title}' takes no part in closing it; its unsaved changes cannot be saved when it closes.")]
    private static partial void LogEditorNotClosable(ILogger logger, string title);

    private void LogEditorNotClosable(string title) => LogEditorNotClosable(this.logger, title);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "'{Title}' did not check its file: {Reason}")]
    private static partial void LogFileCheckFailed(ILogger logger, string title, string reason);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogFileCheckFailed(string title, string reason) => LogFileCheckFailed(this.logger, title, reason);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "'{Title}' followed a relocation of its file from '{From}'.")]
    private static partial void LogFollowedRelocation(ILogger logger, string title, FileSystemPath from);

    private void LogFollowedRelocation(string title, FileSystemPath from) => LogFollowedRelocation(this.logger, title, from);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Closed '{Title}', whose file '{Path}' was deleted.")]
    private static partial void LogDeletedDocumentClosed(ILogger logger, string title, FileSystemPath path);

    private void LogDeletedDocumentClosed(string title, FileSystemPath path) => LogDeletedDocumentClosed(this.logger, title, path);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "'{Title}' stayed open although its file '{Path}' was deleted.")]
    private static partial void LogDeletedDocumentNotClosed(ILogger logger, string title, FileSystemPath path);

    private void LogDeletedDocumentNotClosed(string title, FileSystemPath path) => LogDeletedDocumentNotClosed(this.logger, title, path);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "{Operation} did not run: the user kept {Count} unsaved documents unsaved.")]
    private static partial void LogSaveBeforeCancelled(ILogger logger, SaveBeforeOperation operation, int count);

    private void LogSaveBeforeCancelled(SaveBeforeOperation operation, int count) => LogSaveBeforeCancelled(this.logger, operation, count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "{Operation} did not run: '{Title}' was not saved.")]
    private static partial void LogSaveBeforeFailed(ILogger logger, SaveBeforeOperation operation, string title);

    private void LogSaveBeforeFailed(SaveBeforeOperation operation, string title) => LogSaveBeforeFailed(this.logger, operation, title);
}
