// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;

namespace DroidNet.Config.Sources;

/// <summary>
/// Logging helpers for <see cref="JsonSettingsSource"/>.
/// </summary>
public sealed partial class JsonSettingsSource
{
    [LoggerMessage(
        EventId = 32001,
        Level = LogLevel.Debug,
        Message = "Loading settings from source '{SourceId}' (path: '{SourcePath}', reload: {Reload}).")]
    [ExcludeFromCodeCoverage]
    private static partial void LogLoadRequested(ILogger logger, string sourceId, string sourcePath, bool reload);

    [LoggerMessage(
        EventId = 32002,
        Level = LogLevel.Information,
        Message = "Loaded {SectionCount} section(s) from source '{SourceId}'.")]
    [ExcludeFromCodeCoverage]
    private static partial void LogLoadSucceeded(ILogger logger, string sourceId, int sectionCount);

    [LoggerMessage(
        EventId = 32003,
        Level = LogLevel.Error,
        Message = "Failed to load settings for source '{SourceId}'. {Message}")]
    [ExcludeFromCodeCoverage]
    private static partial void LogLoadFailed(ILogger logger, string sourceId, string message, Exception exception);

    [LoggerMessage(
        EventId = 32004,
        Level = LogLevel.Debug,
        Message = "Persisting {SectionCount} section(s) for source '{SourceId}'.")]
    [ExcludeFromCodeCoverage]
    private static partial void LogSaveRequested(ILogger logger, string sourceId, int sectionCount);

    [LoggerMessage(
        EventId = 32005,
        Level = LogLevel.Information,
        Message = "Persisted settings for source '{SourceId}' ({SectionCount} section(s)).")]
    [ExcludeFromCodeCoverage]
    private static partial void LogSaveSucceeded(ILogger logger, string sourceId, int sectionCount);

    [LoggerMessage(
        EventId = 32006,
        Level = LogLevel.Error,
        Message = "Failed to persist settings for source '{SourceId}'. {Message}")]
    [ExcludeFromCodeCoverage]
    private static partial void LogSaveFailed(ILogger logger, string sourceId, string message, Exception exception);

    [LoggerMessage(
        EventId = 32007,
        Level = LogLevel.Debug,
        Message = "Validating {SectionCount} section(s) for source '{SourceId}'.")]
    [ExcludeFromCodeCoverage]
    private static partial void LogValidationRequested(ILogger logger, string sourceId, int sectionCount);

    [LoggerMessage(
        EventId = 32008,
        Level = LogLevel.Information,
        Message = "Validation succeeded for source '{SourceId}' ({SectionCount} section(s)).")]
    [ExcludeFromCodeCoverage]
    private static partial void LogValidationSucceeded(ILogger logger, string sourceId, int sectionCount);

    [LoggerMessage(
        EventId = 32009,
        Level = LogLevel.Warning,
        Message = "Validation failed for source '{SourceId}'. {Message}")]
    [ExcludeFromCodeCoverage]
    private static partial void LogValidationFailed(ILogger logger, string sourceId, string message, Exception exception);

    [LoggerMessage(
        EventId = 32010,
        Level = LogLevel.Information,
        Message = "No settings file found for source '{SourceId}' at '{SourcePath}'. Returning empty payload.")]
    [ExcludeFromCodeCoverage]
    private static partial void LogFileMissing(ILogger logger, string sourceId, string sourcePath);

    [LoggerMessage(
        EventId = 32011,
        Level = LogLevel.Debug,
        Message = "Read {SectionCount} section(s) from source '{SourceId}'.")]
    [ExcludeFromCodeCoverage]
    private static partial void LogReadSections(ILogger logger, string sourceId, int sectionCount);

    [LoggerMessage(
        EventId = 32012,
        Level = LogLevel.Warning,
        Message = "Failed to merge existing sections for source '{SourceId}'. {Message}")]
    [ExcludeFromCodeCoverage]
    private static partial void LogMergeFailed(ILogger logger, string sourceId, string message, Exception exception);

    [LoggerMessage(
        EventId = 32013,
        Level = LogLevel.Information,
        Message = "Created directory '{Directory}' for source '{SourceId}'.")]
    [ExcludeFromCodeCoverage]
    private static partial void LogDirectoryCreated(ILogger logger, string sourceId, string directory);
}
