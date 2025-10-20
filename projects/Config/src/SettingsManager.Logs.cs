// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;

namespace DroidNet.Config;

#pragma warning disable SA1204 // Static elements should appear before instance elements

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
    [ExcludeFromCodeCoverage]
    private static partial void LogAlreadyInitialized(ILogger logger);

    [Conditional("DEBUG")]
    [ExcludeFromCodeCoverage]
    private void LogAlreadyInitialized() => LogAlreadyInitialized(this.logger);

    [LoggerMessage(
        EventId = 2,
        Level = LogLevel.Information,
        Message = "Initializing SettingsManager with {SourceCount} sources")]
    [ExcludeFromCodeCoverage]
    private static partial void LogInitializing(ILogger logger, int sourceCount);

    [ExcludeFromCodeCoverage]
    private void LogInitializing(int sourceCount) => LogInitializing(this.logger, sourceCount);

    [ExcludeFromCodeCoverage]
    private void LogInitializing() => this.LogInitializing(this.sources.Count);

    [LoggerMessage(
        EventId = 3,
        Level = LogLevel.Debug,
        Message = "Loading settings source: {SourceId}")]
    [ExcludeFromCodeCoverage]
    private static partial void LogLoadingSource(ILogger logger, string sourceId);

    [Conditional("DEBUG")]
    [ExcludeFromCodeCoverage]
    private void LogLoadingSource(string sourceId) => LogLoadingSource(this.logger, sourceId);

    [LoggerMessage(
        EventId = 4,
        Level = LogLevel.Information,
        Message = "Successfully loaded source: {SourceId}")]
    [ExcludeFromCodeCoverage]
    private static partial void LogLoadedSource(ILogger logger, string sourceId);

    [ExcludeFromCodeCoverage]
    private void LogLoadedSource(string sourceId) => LogLoadedSource(this.logger, sourceId);

    [LoggerMessage(
        EventId = 5,
        Level = LogLevel.Warning,
        Message = "Failed to load source {SourceId}: {ErrorMessage}")]
    [ExcludeFromCodeCoverage]
    private static partial void LogFailedToLoadSource(ILogger logger, string sourceId, string? errorMessage);

    [ExcludeFromCodeCoverage]
    private void LogFailedToLoadSource(string sourceId, string? errorMessage) => LogFailedToLoadSource(this.logger, sourceId, errorMessage);

    [LoggerMessage(
        EventId = 7,
        Level = LogLevel.Information,
        Message = "SettingsManager initialization complete")]
    [ExcludeFromCodeCoverage]
    private static partial void LogInitializationComplete(ILogger logger);

    [ExcludeFromCodeCoverage]
    private void LogInitializationComplete() => LogInitializationComplete(this.logger);

    [LoggerMessage(
        EventId = 8,
        Level = LogLevel.Debug,
        Message = "Creating new settings service for type: {SettingsType}")]
    [ExcludeFromCodeCoverage]
    private static partial void LogCreatingService(ILogger logger, string settingsType);

    [Conditional("DEBUG")]
    [ExcludeFromCodeCoverage]
    private void LogCreatingService(string settingsType) => LogCreatingService(this.logger, settingsType);

    [LoggerMessage(
        EventId = 9,
        Level = LogLevel.Information,
        Message = "Reloading all settings sources")]
    [ExcludeFromCodeCoverage]
    private static partial void LogReloadingAllSources(ILogger logger);

    [ExcludeFromCodeCoverage]
    private void LogReloadingAllSources() => LogReloadingAllSources(this.logger);

    [LoggerMessage(
        EventId = 13,
        Level = LogLevel.Debug,
        Message = "Service instance will handle reload: {ServiceType}")]
    [ExcludeFromCodeCoverage]
    private static partial void LogServiceHandlesReload(ILogger logger, string serviceType);

    [Conditional("DEBUG")]
    [ExcludeFromCodeCoverage]
    private void LogServiceHandlesReload(string serviceType) => LogServiceHandlesReload(this.logger, serviceType);

    [LoggerMessage(
        EventId = 14,
        Level = LogLevel.Warning,
        Message = "Failed to deserialize settings for {SettingsType} from section {SectionName}")]
    [ExcludeFromCodeCoverage]
    private static partial void LogSettingsDeserializationFailed(ILogger logger, string sectionName, Type settingsType);

    [ExcludeFromCodeCoverage]
    private void LogSettingsDeserializationFailed(string sectionName, Type settingsType) => LogSettingsDeserializationFailed(this.logger, sectionName, settingsType);

    [LoggerMessage(
        EventId = 16,
        Level = LogLevel.Information,
        Message = "Added new settings source: {SourceId}")]
    [ExcludeFromCodeCoverage]
    private static partial void LogAddedSource(ILogger logger, string sourceId);

    [ExcludeFromCodeCoverage]
    private void LogAddedSource(string sourceId) => LogAddedSource(this.logger, sourceId);

    [LoggerMessage(
        EventId = 17,
        Level = LogLevel.Information,
        Message = "Removed settings source: {SourceId}")]
    [ExcludeFromCodeCoverage]
    private static partial void LogRemovedSource(ILogger logger, string sourceId);

    [ExcludeFromCodeCoverage]
    private void LogRemovedSource(string sourceId) => LogRemovedSource(this.logger, sourceId);

    [LoggerMessage(
        EventId = 18,
        Level = LogLevel.Debug,
        Message = "Disposing SettingsManager")]
    [ExcludeFromCodeCoverage]
    private static partial void LogDisposingManager(ILogger logger);

    [Conditional("DEBUG")]
    [ExcludeFromCodeCoverage]
    private void LogDisposingManager() => LogDisposingManager(this.logger);

    [LoggerMessage(
        EventId = 19,
        Level = LogLevel.Debug,
        Message = "Loaded settings for {SettingsType} from source {SourceId}")]
    [ExcludeFromCodeCoverage]
    private static partial void LogLoadedSettingsForType(ILogger logger, string settingsType, string sourceId);

    [Conditional("DEBUG")]
    [ExcludeFromCodeCoverage]
    private void LogLoadedSettingsForType(string settingsType, string sourceId) => LogLoadedSettingsForType(this.logger, settingsType, sourceId);

    [LoggerMessage(
        EventId = 21,
        Level = LogLevel.Debug,
        Message = "Saved settings for {SettingsType} to source {SourceId}")]
    [ExcludeFromCodeCoverage]
    private static partial void LogSavedSettingsForType(ILogger logger, string settingsType, string sourceId);

    [Conditional("DEBUG")]
    [ExcludeFromCodeCoverage]
    private void LogSavedSettingsForType(string settingsType, string sourceId) => LogSavedSettingsForType(this.logger, settingsType, sourceId);

    [LoggerMessage(
        EventId = 22,
        Level = LogLevel.Warning,
        Message = "Failed to save settings for {SettingsType} to source {SourceId}: {ErrorMessage}")]
    [ExcludeFromCodeCoverage]
    private static partial void LogFailedToSaveSettingsForType(ILogger logger, string settingsType, string sourceId, string? errorMessage);

    [ExcludeFromCodeCoverage]
    private void LogFailedToSaveSettingsForType(string settingsType, string sourceId, string? errorMessage) => LogFailedToSaveSettingsForType(this.logger, settingsType, sourceId, errorMessage);

    [LoggerMessage(
        EventId = 23,
        Level = LogLevel.Error,
        Message = "Exception saving settings for {SettingsType} to source {SourceId}")]
    [ExcludeFromCodeCoverage]
    private static partial void LogExceptionSavingSettingsForType(ILogger logger, Exception exception, string settingsType, string sourceId);

    [ExcludeFromCodeCoverage]
    private void LogExceptionSavingSettingsForType(Exception exception, string settingsType, string sourceId) => LogExceptionSavingSettingsForType(this.logger, exception, settingsType, sourceId);
}
