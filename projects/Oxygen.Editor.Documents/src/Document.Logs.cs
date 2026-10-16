// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Documents;

/// <summary>Logging methods for <see cref="Document"/>.</summary>
public sealed partial class Document
{
    [LoggerMessage(Level = LogLevel.Information, Message = "Saved {Kind} '{Title}' to '{Path}'.")]
    private static partial void LogSaved(ILogger logger, string kind, string title, string path);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Did not save {Kind} '{Title}': {Reason}")]
    private static partial void LogSaveRefused(ILogger logger, string kind, string title, string reason);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Did not save {Kind} '{Title}': '{Path}' changed since it was read or saved. {Details}")]
    private static partial void LogSaveConflict(ILogger logger, string kind, string title, string path, string details);

    [LoggerMessage(Level = LogLevel.Information, Message = "Saved a copy of {Kind} '{Title}' to '{Path}'.")]
    private static partial void LogCopySaved(ILogger logger, string kind, string title, string path);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Did not save a copy of {Kind} '{Title}': '{Path}' exists. {Details}")]
    private static partial void LogCopyConflict(ILogger logger, string kind, string title, string path, string details);

    [LoggerMessage(Level = LogLevel.Information, Message = "Reloaded {Kind} '{Title}' from '{Path}'; its edits and its history were discarded.")]
    private static partial void LogReloaded(ILogger logger, string kind, string title, string path);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Did not reload {Kind} '{Title}' from '{Path}': {Problem}")]
    private static partial void LogReloadFailed(ILogger logger, string kind, string title, string path, string problem);

    [LoggerMessage(Level = LogLevel.Warning, Message = "{Kind} '{Title}' is missing on disk at '{Path}'; it stays open, and saving writes it again.")]
    private static partial void LogMissing(ILogger logger, string kind, string title, string path);

    [LoggerMessage(Level = LogLevel.Information, Message = "'{Path}' holds {Kind} '{Title}' as it was read or saved again.")]
    private static partial void LogRestored(ILogger logger, string kind, string title, string path);

    [LoggerMessage(Level = LogLevel.Warning, Message = "'{Path}' changed outside the editor while {Kind} '{Title}' has edits or could not be read again; its edits are kept.")]
    private static partial void LogChangedOnDisk(ILogger logger, string kind, string title, string path);

    [LoggerMessage(Level = LogLevel.Information, Message = "{Kind} '{Title}' keeps its edits over the change to '{Path}'; its next save replaces the file.")]
    private static partial void LogKeptMine(ILogger logger, string kind, string title, string path);

    [LoggerMessage(Level = LogLevel.Information, Message = "{Kind} '{Title}' follows its file from '{From}' to '{Path}'.")]
    private static partial void LogFollowed(ILogger logger, string kind, string title, string from, string path);

    private void LogSaved() => LogSaved(this.logger, this.Kind.Name, this.Title, this.File.Path);

    private void LogSaveRefused(string reason) => LogSaveRefused(this.logger, this.Kind.Name, this.Title, reason);

    private void LogSaveConflict(string details) => LogSaveConflict(this.logger, this.Kind.Name, this.Title, this.File.Path, details);

    private void LogCopySaved(string path) => LogCopySaved(this.logger, this.Kind.Name, this.Title, path);

    private void LogCopyConflict(string path, string details) => LogCopyConflict(this.logger, this.Kind.Name, this.Title, path, details);

    private void LogReloaded() => LogReloaded(this.logger, this.Kind.Name, this.Title, this.File.Path);

    private void LogReloadFailed(string problem) => LogReloadFailed(this.logger, this.Kind.Name, this.Title, this.File.Path, problem);

    private void LogMissing() => LogMissing(this.logger, this.Kind.Name, this.Title, this.File.Path);

    private void LogRestored() => LogRestored(this.logger, this.Kind.Name, this.Title, this.File.Path);

    private void LogChangedOnDisk() => LogChangedOnDisk(this.logger, this.Kind.Name, this.Title, this.File.Path);

    private void LogKeptMine() => LogKeptMine(this.logger, this.Kind.Name, this.Title, this.File.Path);

    private void LogFollowed(string from) => LogFollowed(this.logger, this.Kind.Name, this.Title, from, this.File.Path);
}
