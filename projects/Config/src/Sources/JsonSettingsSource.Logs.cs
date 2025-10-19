// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Config.Sources;

/// <summary>
/// Settings source that persists settings to JSON files with atomic write operations.
/// </summary>
public partial class JsonSettingsSource
{
    [LoggerMessage(
        EventId = 1,
        Level = LogLevel.Debug,
        Message = "Reading settings from file: {FilePath}")]
    [Conditional("DEBUG")]
    private static partial void LogReadingFile(ILogger logger, string filePath);

    [LoggerMessage(
        EventId = 2,
        Level = LogLevel.Information,
        Message = "File not found, returning empty settings: {FilePath}")]
    private static partial void LogFileNotFound(ILogger logger, string filePath);

    [LoggerMessage(
        EventId = 3,
        Level = LogLevel.Warning,
        Message = "File is empty, returning empty settings: {FilePath}")]
    private static partial void LogEmptyFile(ILogger logger, string filePath);

    [LoggerMessage(
        EventId = 4,
        Level = LogLevel.Warning,
        Message = "Invalid JSON format in file: {FilePath}")]
    private static partial void LogInvalidJsonFormat(ILogger logger, string filePath);

    [LoggerMessage(
        EventId = 5,
        Level = LogLevel.Debug,
        Message = "Successfully read {SectionCount} section(s) from: {FilePath}")]
    [Conditional("DEBUG")]
    private static partial void LogReadSuccess(ILogger logger, string filePath, int sectionCount);

    [LoggerMessage(
        EventId = 6,
        Level = LogLevel.Error,
        Message = "JSON deserialization error reading from: {FilePath}")]
    private static partial void LogJsonDeserializationError(ILogger logger, Exception exception, string filePath);

    [LoggerMessage(
        EventId = 7,
        Level = LogLevel.Error,
        Message = "I/O error accessing file: {FilePath}")]
    private static partial void LogIoError(ILogger logger, Exception exception, string filePath);

    [LoggerMessage(
        EventId = 8,
        Level = LogLevel.Error,
        Message = "Access denied to file: {FilePath}")]
    private static partial void LogAccessDenied(ILogger logger, Exception exception, string filePath);

    [LoggerMessage(
        EventId = 9,
        Level = LogLevel.Error,
        Message = "Unexpected error accessing file: {FilePath}")]
    private static partial void LogUnexpectedError(ILogger logger, Exception exception, string filePath);

    [LoggerMessage(
        EventId = 10,
        Level = LogLevel.Debug,
        Message = "Writing {SectionCount} section(s) to file: {FilePath}")]
    [Conditional("DEBUG")]
    private static partial void LogWritingFile(ILogger logger, string filePath, int sectionCount);

    [LoggerMessage(
        EventId = 11,
        Level = LogLevel.Information,
        Message = "Created directory: {Directory}")]
    private static partial void LogDirectoryCreated(ILogger logger, string directory);

    [LoggerMessage(
        EventId = 12,
        Level = LogLevel.Debug,
        Message = "Created backup: {BackupFilePath}")]
    [Conditional("DEBUG")]
    private static partial void LogBackupCreated(ILogger logger, string backupFilePath);

    [LoggerMessage(
        EventId = 13,
        Level = LogLevel.Information,
        Message = "Successfully wrote settings to: {FilePath}")]
    private static partial void LogWriteSuccess(ILogger logger, string filePath);

    [LoggerMessage(
        EventId = 14,
        Level = LogLevel.Warning,
        Message = "Restored backup to: {FilePath}")]
    private static partial void LogBackupRestored(ILogger logger, string filePath);

    [LoggerMessage(
        EventId = 15,
        Level = LogLevel.Error,
        Message = "Failed to restore backup from: {BackupFilePath}")]
    private static partial void LogBackupRestoreFailed(ILogger logger, Exception exception, string backupFilePath);

    [LoggerMessage(
        EventId = 16,
        Level = LogLevel.Error,
        Message = "JSON serialization error writing to: {FilePath}")]
    private static partial void LogJsonSerializationError(ILogger logger, Exception exception, string filePath);

    [LoggerMessage(
        EventId = 17,
        Level = LogLevel.Debug,
        Message = "Validating {SectionCount} section(s) for JSON compatibility")]
    [Conditional("DEBUG")]
    private static partial void LogValidatingContent(ILogger logger, int sectionCount);

    [LoggerMessage(
        EventId = 18,
        Level = LogLevel.Debug,
        Message = "Successfully validated {SectionCount} section(s)")]
    [Conditional("DEBUG")]
    private static partial void LogValidationSuccess(ILogger logger, int sectionCount);

    [LoggerMessage(
        EventId = 19,
        Level = LogLevel.Warning,
        Message = "Validation failed")]
    private static partial void LogValidationFailed(ILogger logger, Exception exception);

    [LoggerMessage(
        EventId = 20,
        Level = LogLevel.Error,
        Message = "Unexpected error during validation")]
    private static partial void LogUnexpectedValidationError(ILogger logger, Exception exception);

    [LoggerMessage(
        EventId = 21,
        Level = LogLevel.Information,
        Message = "Reloading settings from: {FilePath}")]
    private static partial void LogReloading(ILogger logger, string filePath);

    [LoggerMessage(
        EventId = 22,
        Level = LogLevel.Information,
        Message = "Successfully reloaded settings from: {FilePath}")]
    private static partial void LogReloadSuccess(ILogger logger, string filePath);

    [LoggerMessage(
        EventId = 23,
        Level = LogLevel.Error,
        Message = "Failed to reload settings from: {FilePath}")]
    private static partial void LogReloadFailed(ILogger logger, Exception exception, string filePath);

    [LoggerMessage(
        EventId = 24,
        Level = LogLevel.Warning,
        Message = "Cannot watch for changes, invalid path: {FilePath}")]
    private static partial void LogCannotWatchInvalidPath(ILogger logger, string filePath);

    [LoggerMessage(
        EventId = 25,
        Level = LogLevel.Debug,
        Message = "File changed: {FilePath}, ChangeType: {ChangeType}")]
    [Conditional("DEBUG")]
    private static partial void LogFileChanged(ILogger logger, string filePath, string changeType);

    [LoggerMessage(
        EventId = 26,
        Level = LogLevel.Debug,
        Message = "File renamed from {OldPath} to {NewPath}")]
    [Conditional("DEBUG")]
    private static partial void LogFileRenamed(ILogger logger, string oldPath, string newPath);

    [LoggerMessage(
        EventId = 27,
        Level = LogLevel.Information,
        Message = "Started watching for changes: {FilePath}")]
    private static partial void LogWatchingStarted(ILogger logger, string filePath);

    [LoggerMessage(
        EventId = 28,
        Level = LogLevel.Information,
        Message = "Stopped watching for changes: {FilePath}")]
    private static partial void LogWatchingStopped(ILogger logger, string filePath);

    [LoggerMessage(
        EventId = 29,
        Level = LogLevel.Error,
        Message = "Failed to start watching for changes: {FilePath}")]
    private static partial void LogWatchingFailed(ILogger logger, Exception exception, string filePath);

    [LoggerMessage(
        EventId = 30,
        Level = LogLevel.Debug,
        Message = "Cleaned up temporary file: {TempFilePath}")]
    [Conditional("DEBUG")]
    private static partial void LogTempFileCleanedUp(ILogger logger, string tempFilePath);

    [LoggerMessage(
        EventId = 31,
        Level = LogLevel.Warning,
        Message = "Failed to clean up temporary file: {TempFilePath}")]
    private static partial void LogTempFileCleanupFailed(ILogger logger, Exception exception, string tempFilePath);

    [LoggerMessage(
        EventId = 32,
        Level = LogLevel.Error,
        Message = "Settings file is missing required 'metadata' section: {FilePath}")]
    private static partial void LogMissingMetadata(ILogger logger, string filePath);
}
