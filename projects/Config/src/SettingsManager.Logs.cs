// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Config;

/// <summary>
/// Central orchestrator that manages all settings sources and provides access to settings services.
/// Implements last-loaded-wins strategy for multi-source composition.
/// </summary>
public partial class SettingsManager
{
    [LoggerMessage(
        EventId = 1,
        Level = LogLevel.Debug,
        Message = "SettingsManager already initialized")]
    [Conditional("DEBUG")]
    private static partial void LogAlreadyInitialized(ILogger logger);

    [LoggerMessage(
        EventId = 2,
        Level = LogLevel.Information,
        Message = "Initializing SettingsManager with {SourceCount} sources")]
    private static partial void LogInitializing(ILogger logger, int sourceCount);

    [LoggerMessage(
        EventId = 3,
        Level = LogLevel.Debug,
        Message = "Loading settings source: {SourceId}")]
    [Conditional("DEBUG")]
    private static partial void LogLoadingSource(ILogger logger, string sourceId);

    [LoggerMessage(
        EventId = 4,
        Level = LogLevel.Information,
        Message = "Successfully loaded source: {SourceId}")]
    private static partial void LogLoadedSource(ILogger logger, string sourceId);

    [LoggerMessage(
        EventId = 5,
        Level = LogLevel.Warning,
        Message = "Failed to load source {SourceId}: {ErrorMessage}")]
    private static partial void LogFailedToLoadSource(ILogger logger, string sourceId, string? errorMessage);

    [LoggerMessage(
        EventId = 6,
        Level = LogLevel.Error,
        Message = "Exception loading source: {SourceId}")]
    private static partial void LogExceptionLoadingSource(ILogger logger, Exception exception, string sourceId);

    [LoggerMessage(
        EventId = 7,
        Level = LogLevel.Information,
        Message = "SettingsManager initialization complete")]
    private static partial void LogInitializationComplete(ILogger logger);

    [LoggerMessage(
        EventId = 8,
        Level = LogLevel.Debug,
        Message = "Creating new settings service for type: {SettingsType}")]
    [Conditional("DEBUG")]
    private static partial void LogCreatingService(ILogger logger, string settingsType);

    [LoggerMessage(
        EventId = 9,
        Level = LogLevel.Information,
        Message = "Reloading all settings sources")]
    private static partial void LogReloadingAllSources(ILogger logger);

    [LoggerMessage(
        EventId = 10,
        Level = LogLevel.Debug,
        Message = "Successfully reloaded source: {SourceId}")]
    [Conditional("DEBUG")]
    private static partial void LogReloadedSource(ILogger logger, string sourceId);

    [LoggerMessage(
        EventId = 11,
        Level = LogLevel.Warning,
        Message = "Failed to reload source {SourceId}: {ErrorMessage}")]
    private static partial void LogFailedToReloadSource(ILogger logger, string sourceId, string? errorMessage);

    [LoggerMessage(
        EventId = 12,
        Level = LogLevel.Error,
        Message = "Exception reloading source: {SourceId}")]
    private static partial void LogExceptionReloadingSource(ILogger logger, Exception exception, string sourceId);

    [LoggerMessage(
        EventId = 13,
        Level = LogLevel.Debug,
        Message = "Service instance will handle reload: {ServiceType}")]
    [Conditional("DEBUG")]
    private static partial void LogServiceHandlesReload(ILogger logger, string serviceType);

    [LoggerMessage(
        EventId = 14,
        Level = LogLevel.Information,
        Message = "Running migrations for all settings services")]
    private static partial void LogRunningMigrations(ILogger logger);

    [LoggerMessage(
        EventId = 15,
        Level = LogLevel.Error,
        Message = "Migration failed for service: {ServiceType}")]
    private static partial void LogMigrationFailed(ILogger logger, Exception exception, string serviceType);

    [LoggerMessage(
        EventId = 16,
        Level = LogLevel.Information,
        Message = "Added new settings source: {SourceId}")]
    private static partial void LogAddedSource(ILogger logger, string sourceId);

    [LoggerMessage(
        EventId = 17,
        Level = LogLevel.Information,
        Message = "Removed settings source: {SourceId}")]
    private static partial void LogRemovedSource(ILogger logger, string sourceId);

    [LoggerMessage(
        EventId = 18,
        Level = LogLevel.Debug,
        Message = "Disposing SettingsManager")]
    [Conditional("DEBUG")]
    private static partial void LogDisposingManager(ILogger logger);

    [LoggerMessage(
        EventId = 19,
        Level = LogLevel.Debug,
        Message = "Loaded settings for {SettingsType} from source {SourceId}")]
    [Conditional("DEBUG")]
    private static partial void LogLoadedSettingsForType(ILogger logger, string settingsType, string sourceId);

    [LoggerMessage(
        EventId = 20,
        Level = LogLevel.Warning,
        Message = "Failed to load settings for {SettingsType} from source {SourceId}")]
    private static partial void LogFailedToLoadSettingsForType(ILogger logger, Exception exception, string settingsType, string sourceId);

    [LoggerMessage(
        EventId = 21,
        Level = LogLevel.Debug,
        Message = "Saved settings for {SettingsType} to source {SourceId}")]
    [Conditional("DEBUG")]
    private static partial void LogSavedSettingsForType(ILogger logger, string settingsType, string sourceId);

    [LoggerMessage(
        EventId = 22,
        Level = LogLevel.Warning,
        Message = "Failed to save settings for {SettingsType} to source {SourceId}: {ErrorMessage}")]
    private static partial void LogFailedToSaveSettingsForType(ILogger logger, string settingsType, string sourceId, string? errorMessage);

    [LoggerMessage(
        EventId = 23,
        Level = LogLevel.Error,
        Message = "Exception saving settings for {SettingsType} to source {SourceId}")]
    private static partial void LogExceptionSavingSettingsForType(ILogger logger, Exception exception, string settingsType, string sourceId);
}
