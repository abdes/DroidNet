// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Security.Cryptography;
using Microsoft.Extensions.Logging;

namespace DroidNet.Config.Sources;

/// <summary>
///     Provides access to configuration data stored in a file, with optional encryption and atomic write semantics.
/// </summary>
public abstract partial class FileSettingsSource
{
    [LoggerMessage(
        EventId = 31001,
        Level = LogLevel.Error,
        Message = "File IO error in `{Operation}`, source='{SourceId}', path='{SourcePath}'")]
    [ExcludeFromCodeCoverage]
    private static partial void LogFileIoError(ILogger logger, string sourceId, string sourcePath, string operation, Exception exception);

    private void LogFileIoError(string operation, Exception ex)
        => LogFileIoError(this.Logger, this.Id, this.path, operation, ex);

    [LoggerMessage(
        EventId = 31002,
        Level = LogLevel.Error,
        Message = "Error reading configuration data from source '{SourceId}' at '{SourcePath}'")]
    [ExcludeFromCodeCoverage]
    private static partial void LogReadError(ILogger logger, string sourceId, string sourcePath, Exception exception);

    private void LogReadError(Exception ex)
        => LogReadError(this.Logger, this.Id, this.path, ex);

    [LoggerMessage(
        EventId = 31003,
        Level = LogLevel.Error,
        Message = "Error writing configuration data from source '{SourceId}' at '{SourcePath}'")]
    [ExcludeFromCodeCoverage]
    private static partial void LogWriteError(ILogger logger, string sourceId, string sourcePath, Exception exception);

    private void LogWriteError(Exception ex)
        => LogWriteError(this.Logger, this.Id, this.path, ex);

    [LoggerMessage(
        EventId = 31004,
        Level = LogLevel.Error,
        Message = "Error {Operation} configuration data from source '{SourceId}' at '{SourcePath}'")]
    [ExcludeFromCodeCoverage]
    private static partial void LogCryptoError(ILogger logger, string sourceId, string sourcePath, string operation, Exception exception);

    private void LogCryptoError(string operation, CryptographicException ex)
        => LogCryptoError(this.Logger, this.Id, this.path, operation, ex);

    [LoggerMessage(
        EventId = 31005,
        Level = LogLevel.Information,
        Message = "Operation '{Operation}' was canceled while processing source '{SourceId}' at '{SourcePath}'")]
    [ExcludeFromCodeCoverage]
    private static partial void LogOperationCancelled(ILogger logger, string sourceId, string sourcePath, string operation);

    private void LogOperationCancelled(string operation)
        => LogOperationCancelled(this.Logger, this.Id, this.path, operation);

    [LoggerMessage(
        EventId = 31006,
        Level = LogLevel.Debug,
        Message = "Moved file '{SourcePath}' to '{DestinationPath}' for source '{SourceId}'")]
    [ExcludeFromCodeCoverage]
    private static partial void LogFileMoved(ILogger logger, string sourceId, string sourcePath, string destinationPath);

    [Conditional("DEBUG")]
    private void LogFileMoved(string sourcePath, string destinationPath)
        => LogFileMoved(this.Logger, this.Id, sourcePath, destinationPath);

    [LoggerMessage(
        EventId = 31007,
        Level = LogLevel.Debug,
        Message = "Deleted file '{FilePath}' for source '{SourceId}'")]
    [ExcludeFromCodeCoverage]
    private static partial void LogFileDeleted(ILogger logger, string sourceId, string filePath);

    [Conditional("DEBUG")]
    private void LogFileDeleted(string filePath)
        => LogFileDeleted(this.Logger, this.Id, filePath);

    [LoggerMessage(
        EventId = 31008,
        Level = LogLevel.Debug,
        Message = "Replaced file '{DestinationPath}' with '{SourcePath}' for source '{SourceId}'")]
    [ExcludeFromCodeCoverage]
    private static partial void LogFileReplaced(ILogger logger, string sourceId, string sourcePath, string destinationPath);

    [Conditional("DEBUG")]
    private void LogFileReplaced(string sourcePath, string destinationPath)
        => LogFileReplaced(this.Logger, this.Id, sourcePath, destinationPath);

    [LoggerMessage(
        EventId = 31009,
        Level = LogLevel.Information,
        Message = "Restored original file '{DestinationPath}' from backup '{BackupPath}' for source '{SourceId}'")]
    [ExcludeFromCodeCoverage]
    private static partial void LogRestoredOriginal(ILogger logger, string sourceId, string backupPath, string destinationPath);

    private void LogRestoredOriginal(string backupPath, string destinationPath)
        => LogRestoredOriginal(this.Logger, this.Id, backupPath, destinationPath);
}
