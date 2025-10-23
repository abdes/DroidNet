// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Config;

/// <summary>
///     Provides a last-added-wins implementation of <see cref="ISettingsManager"/> for multi-source settings composition.
/// </summary>
public partial class SettingsManager
{
    [LoggerMessage(
        EventId = 1,
        Level = LogLevel.Debug,
        Message = "SettingsManager already initialized")]
    private static partial void LogAlreadyInitialized(ILogger logger);

    [Conditional("DEBUG")]
    private void LogAlreadyInitialized() => LogAlreadyInitialized(this.logger);

    [LoggerMessage(
        EventId = 2,
        Level = LogLevel.Information,
        Message = "Initializing SettingsManager with {SourceCount} sources")]
    private static partial void LogInitializing(ILogger logger, int sourceCount);

    [LoggerMessage(
        EventId = 3,
        Level = LogLevel.Warning,
        Message = "SettingsManager initialized with no sources configured")]
    private static partial void LogInitializingNoSources(ILogger logger);

    private void LogInitializing()
    {
        if (this.sources.Count == 0)
        {
            LogInitializingNoSources(this.logger);
            return;
        }

        LogInitializing(this.logger, this.sources.Count);
    }

    [LoggerMessage(
        EventId = 4,
        Level = LogLevel.Debug,
        Message = "Loading settings source: {SourceId}")]
    private static partial void LogLoadingSource(ILogger logger, string sourceId);

    [Conditional("DEBUG")]
    private void LogLoadingSource(string sourceId) => LogLoadingSource(this.logger, sourceId);

    [LoggerMessage(
        EventId = 5,
        Level = LogLevel.Information,
        Message = "Successfully loaded source: {SourceId}")]
    private static partial void LogLoadedSource(ILogger logger, string sourceId);

    private void LogLoadedSource(string sourceId) => LogLoadedSource(this.logger, sourceId);

    [LoggerMessage(
        EventId = 6,
        Level = LogLevel.Warning,
        Message = "Failed to load source {SourceId}: {ErrorMessage}")]
    private static partial void LogFailedToLoadSource(ILogger logger, string sourceId, string? errorMessage);

    private void LogFailedToLoadSource(string sourceId, string? errorMessage) => LogFailedToLoadSource(this.logger, sourceId, errorMessage);

    [LoggerMessage(
        EventId = 7,
        Level = LogLevel.Information,
        Message = "SettingsManager initialization complete")]
    private static partial void LogInitializationComplete(ILogger logger);

    private void LogInitializationComplete() => LogInitializationComplete(this.logger);

    [LoggerMessage(
        EventId = 8,
        Level = LogLevel.Debug,
        Message = "Creating new settings service for type: {SettingsType}")]
    private static partial void LogCreatingService(ILogger logger, string settingsType);

    [Conditional("DEBUG")]
    private void LogCreatingService(string settingsType) => LogCreatingService(this.logger, settingsType);

    [LoggerMessage(
        EventId = 9,
        Level = LogLevel.Information,
        Message = "Reloading all settings sources")]
    private static partial void LogReloadingAllSources(ILogger logger);

    private void LogReloadingAllSources() => LogReloadingAllSources(this.logger);

    [LoggerMessage(
        EventId = 10,
        Level = LogLevel.Debug,
        Message = "Service instance will handle reload: {ServiceType}")]
    private static partial void LogServiceHandlesReload(ILogger logger, string serviceType);

    [Conditional("DEBUG")]
    private void LogServiceHandlesReload(string serviceType) => LogServiceHandlesReload(this.logger, serviceType);

    [LoggerMessage(
        EventId = 11,
        Level = LogLevel.Warning,
        Message = "Failed to deserialize settings for {SettingsType} from section {SectionName}")]
    private static partial void LogSettingsDeserializationFailed(ILogger logger, string sectionName, Type settingsType);

    private void LogSettingsDeserializationFailed(string sectionName, Type settingsType) => LogSettingsDeserializationFailed(this.logger, sectionName, settingsType);

    [LoggerMessage(
        EventId = 12,
        Level = LogLevel.Information,
        Message = "Added new settings source: {SourceId}")]
    private static partial void LogAddedSource(ILogger logger, string sourceId);

    private void LogAddedSource(string sourceId) => LogAddedSource(this.logger, sourceId);

    [LoggerMessage(
        EventId = 13,
        Level = LogLevel.Information,
        Message = "Removed settings source: {SourceId}")]
    private static partial void LogRemovedSource(ILogger logger, string sourceId);

    private void LogRemovedSource(string sourceId) => LogRemovedSource(this.logger, sourceId);

    [LoggerMessage(
        EventId = 14,
        Level = LogLevel.Debug,
        Message = "Disposing SettingsManager")]
    private static partial void LogDisposingManager(ILogger logger);

    [Conditional("DEBUG")]
    private void LogDisposingManager() => LogDisposingManager(this.logger);

    [LoggerMessage(
        EventId = 15,
        Level = LogLevel.Debug,
        Message = "Loaded settings for {SettingsType} from source {SourceId}")]
    private static partial void LogLoadedSettingsForType(ILogger logger, string settingsType, string sourceId);

    [Conditional("DEBUG")]
    private void LogLoadedSettingsForType(string settingsType, string sourceId) => LogLoadedSettingsForType(this.logger, settingsType, sourceId);

    [LoggerMessage(
        EventId = 16,
        Level = LogLevel.Debug,
        Message = "Saved settings for {SettingsType} to source {SourceId}")]
    private static partial void LogSavedSettingsForType(ILogger logger, string settingsType, string sourceId);

    [Conditional("DEBUG")]
    private void LogSavedSettingsForType(string settingsType, string sourceId) => LogSavedSettingsForType(this.logger, settingsType, sourceId);

    [LoggerMessage(
        EventId = 17,
        Level = LogLevel.Warning,
        Message = "Failed to save settings for {SettingsType} to source {SourceId}: {ErrorMessage}")]
    private static partial void LogFailedToSaveSettingsForType(ILogger logger, string settingsType, string sourceId, string? errorMessage);

    private void LogFailedToSaveSettingsForType(string settingsType, string sourceId, string? errorMessage) => LogFailedToSaveSettingsForType(this.logger, settingsType, sourceId, errorMessage);

    [LoggerMessage(
        EventId = 18,
        Level = LogLevel.Error,
        Message = "Exception saving settings for {SettingsType} to source {SourceId}")]
    private static partial void LogExceptionSavingSettingsForType(ILogger logger, Exception exception, string settingsType, string sourceId);

    private void LogExceptionSavingSettingsForType(Exception exception, string settingsType, string sourceId) => LogExceptionSavingSettingsForType(this.logger, exception, settingsType, sourceId);

    [LoggerMessage(
        EventId = 19,
        Level = LogLevel.Debug,
        Message = "Saving section {SectionName} to winning source: {SourceId}")]
    private static partial void LogSavingToWinningSource(ILogger logger, string sectionName, string sourceId);

    [Conditional("DEBUG")]
    private void LogSavingToWinningSource(string sectionName, string sourceId) => LogSavingToWinningSource(this.logger, sectionName, sourceId);

    [LoggerMessage(
        EventId = 20,
        Level = LogLevel.Debug,
        Message = "GetService requested for {SettingsType}")]
    private static partial void LogGetServiceRequested(ILogger logger, string settingsType);

    [Conditional("DEBUG")]
    private void LogGetServiceRequested(string settingsType) => LogGetServiceRequested(this.logger, settingsType);

    [LoggerMessage(
        EventId = 21,
        Level = LogLevel.Debug,
        Message = "Returning existing service instance for {SettingsType}")]
    private static partial void LogReturningExistingService(ILogger logger, string settingsType);

    [Conditional("DEBUG")]
    private void LogReturningExistingService(string settingsType) => LogReturningExistingService(this.logger, settingsType);

    [LoggerMessage(
        EventId = 22,
        Level = LogLevel.Debug,
        Message = "Registered service {SettingsType}. Total services: {ServiceCount}")]
    private static partial void LogServiceRegistered(ILogger logger, string settingsType, int serviceCount);

    [Conditional("DEBUG")]
    private void LogServiceRegistered(string settingsType, int serviceCount) => LogServiceRegistered(this.logger, settingsType, serviceCount);

    [LoggerMessage(
        EventId = 23,
        Level = LogLevel.Debug,
        Message = "Saving settings requested for section {SectionName}")]
    private static partial void LogSaveSettingsRequested(ILogger logger, string sectionName);

    [Conditional("DEBUG")]
    private void LogSaveSettingsRequested(string sectionName) => LogSaveSettingsRequested(this.logger, sectionName);

    [LoggerMessage(
        EventId = 24,
        Level = LogLevel.Debug,
        Message = "Applying settings for section {SectionName}")]
    private static partial void LogApplyingSettings(ILogger logger, string sectionName);

    [Conditional("DEBUG")]
    private void LogApplyingSettings(string sectionName) => LogApplyingSettings(this.logger, sectionName);

    [LoggerMessage(
        EventId = 25,
        Level = LogLevel.Debug,
        Message = "Applying cached section {SectionName} with value type {ValueType}")]
    private static partial void LogApplyingCachedSection(ILogger logger, string sectionName, string valueType);

    [Conditional("DEBUG")]
    private void LogApplyingCachedSection(string sectionName, string valueType) => LogApplyingCachedSection(this.logger, sectionName, valueType);

    [LoggerMessage(
        EventId = 26,
        Level = LogLevel.Debug,
        Message = "Cached JsonElement for section {SectionName} contains property {PropertyName} = {PropertyValue}")]
    private static partial void LogCachedJsonElementProperty(ILogger logger, string sectionName, string propertyName, string? propertyValue);

    [Conditional("DEBUG")]
    private void LogCachedJsonElementProperty(string sectionName, string propertyName, string? propertyValue)
        => LogCachedJsonElementProperty(this.logger, sectionName, propertyName, propertyValue);

    [LoggerMessage(
        EventId = 27,
        Level = LogLevel.Debug,
        Message = "No cached data found for section {SectionName}; keeping service defaults")]
    private static partial void LogNoCachedData(ILogger logger, string sectionName);

    [Conditional("DEBUG")]
    private void LogNoCachedData(string sectionName) => LogNoCachedData(this.logger, sectionName);

    [LoggerMessage(
        EventId = 28,
        Level = LogLevel.Debug,
        Message = "Subscribing to source change events for {SourceId}")]
    private static partial void LogSubscribingToSource(ILogger logger, string sourceId);

    [Conditional("DEBUG")]
    private void LogSubscribingToSource(string sourceId) => LogSubscribingToSource(this.logger, sourceId);

    [LoggerMessage(
        EventId = 29,
        Level = LogLevel.Debug,
        Message = "Unsubscribing from source change events for {SourceId}")]
    private static partial void LogUnsubscribingFromSource(ILogger logger, string sourceId);

    [Conditional("DEBUG")]
    private void LogUnsubscribingFromSource(string sourceId) => LogUnsubscribingFromSource(this.logger, sourceId);

    [LoggerMessage(
        EventId = 30,
        Level = LogLevel.Debug,
        Message = "Source change event received for {SourceId} with change type {ChangeType}")]
    private static partial void LogSourceChangedEventReceived(ILogger logger, string sourceId, SourceChangeType changeType);

    [Conditional("DEBUG")]
    private void LogSourceChangedEventReceived(string sourceId, SourceChangeType changeType)
        => LogSourceChangedEventReceived(this.logger, sourceId, changeType);

    [LoggerMessage(
        EventId = 31,
        Level = LogLevel.Debug,
        Message = "Suppressing change notification for {SourceId} due to pending operation")]
    private static partial void LogSuppressingChangeNotification(ILogger logger, string sourceId);

    [Conditional("DEBUG")]
    private void LogSuppressingChangeNotification(string sourceId) => LogSuppressingChangeNotification(this.logger, sourceId);

    [LoggerMessage(
        EventId = 32,
        Level = LogLevel.Information,
        Message = "Source {SourceId} was not found while handling change notification")]
    private static partial void LogSourceNotFound(ILogger logger, string sourceId);

    private void LogSourceNotFound(string sourceId) => LogSourceNotFound(this.logger, sourceId);

    [LoggerMessage(
        EventId = 33,
        Level = LogLevel.Debug,
        Message = "Reloaded source {SourceId}. Sections before: {SectionsBefore}, after: {SectionsAfter}, affected: {AffectedSections}")]
    private static partial void LogSourceReloadSummary(ILogger logger, string sourceId, int sectionsBefore, int sectionsAfter, int affectedSections);

    [Conditional("DEBUG")]
    private void LogSourceReloadSummary(string sourceId, int sectionsBefore, int sectionsAfter, int affectedSections)
        => LogSourceReloadSummary(this.logger, sourceId, sectionsBefore, sectionsAfter, affectedSections);

    [LoggerMessage(
        EventId = 34,
        Level = LogLevel.Debug,
        Message = "Removing section {SectionName} previously provided by {SourceId}")]
    private static partial void LogRemovingStaleSection(ILogger logger, string sectionName, string sourceId);

    [Conditional("DEBUG")]
    private void LogRemovingStaleSection(string sectionName, string sourceId)
        => LogRemovingStaleSection(this.logger, sectionName, sourceId);

    [LoggerMessage(
        EventId = 35,
        Level = LogLevel.Debug,
        Message = "Cached section {SectionName} with data from source {SourceId}")]
    private static partial void LogSectionCached(ILogger logger, string sectionName, string sourceId);

    [Conditional("DEBUG")]
    private void LogSectionCached(string sectionName, string sourceId) => LogSectionCached(this.logger, sectionName, sourceId);

    [LoggerMessage(
        EventId = 36,
        Level = LogLevel.Debug,
        Message = "Skipped caching section {SectionName} from source {SourceId}; current owner is {CurrentOwner}")]
    private static partial void LogSectionCacheSkipped(ILogger logger, string sectionName, string sourceId, string? currentOwner);

    [Conditional("DEBUG")]
    private void LogSectionCacheSkipped(string sectionName, string sourceId, string? currentOwner)
        => LogSectionCacheSkipped(this.logger, sectionName, sourceId, currentOwner);

    [LoggerMessage(
        EventId = 37,
        Level = LogLevel.Warning,
        Message = "Service {ServiceType} does not expose a SectionName property")]
    private static partial void LogServiceMissingSectionName(ILogger logger, string serviceType);

    private void LogServiceMissingSectionName(string serviceType) => LogServiceMissingSectionName(this.logger, serviceType);

    [LoggerMessage(
        EventId = 38,
        Level = LogLevel.Warning,
        Message = "Service {ServiceType} has a null SectionName value")]
    private static partial void LogServiceSectionNameNull(ILogger logger, string serviceType);

    private void LogServiceSectionNameNull(string serviceType) => LogServiceSectionNameNull(this.logger, serviceType);

    [LoggerMessage(
        EventId = 39,
        Level = LogLevel.Warning,
        Message = "Service {ServiceType} does not expose an ApplyProperties method")]
    private static partial void LogServiceMissingApplyPropertiesMethod(ILogger logger, string serviceType);

    private void LogServiceMissingApplyPropertiesMethod(string serviceType)
        => LogServiceMissingApplyPropertiesMethod(this.logger, serviceType);

    [LoggerMessage(
        EventId = 40,
        Level = LogLevel.Debug,
        Message = "Updating {ServiceCount} impacted services")]
    private static partial void LogUpdatingImpactedServices(ILogger logger, int serviceCount);

    [Conditional("DEBUG")]
    private void LogUpdatingImpactedServices(int serviceCount) => LogUpdatingImpactedServices(this.logger, serviceCount);

    [LoggerMessage(
        EventId = 41,
        Level = LogLevel.Debug,
        Message = "Service {ServiceType} section {SectionName} refreshed from cache")]
    private static partial void LogServiceCacheHit(ILogger logger, string serviceType, string sectionName);

    [Conditional("DEBUG")]
    private void LogServiceCacheHit(string serviceType, string sectionName)
        => LogServiceCacheHit(this.logger, serviceType, sectionName);

    [LoggerMessage(
        EventId = 42,
        Level = LogLevel.Information,
        Message = "Service {ServiceType} section {SectionName} reset to defaults (no cached data)")]
    private static partial void LogServiceResetToDefaults(ILogger logger, string serviceType, string sectionName);

    private void LogServiceResetToDefaults(string serviceType, string sectionName)
        => LogServiceResetToDefaults(this.logger, serviceType, sectionName);

    [LoggerMessage(
        EventId = 43,
        Level = LogLevel.Warning,
        Message = "No winning source for section {SectionName}, saving to first available source: {SourceId}")]
    private static partial void LogSavingToFirstAvailableSource(ILogger logger, string sectionName, string sourceId);

    private void LogSavingToFirstAvailableSource(string sectionName, string sourceId) => LogSavingToFirstAvailableSource(this.logger, sectionName, sourceId);

    [LoggerMessage(
        EventId = 44,
        Level = LogLevel.Debug,
        Message = "Handling source change event: {SourceId}, {ChangeType}")]
    private static partial void LogHandlingSourceChange(ILogger logger, string sourceId, SourceChangeType changeType);

    [Conditional("DEBUG")]
    private void LogHandlingSourceChange(string sourceId, SourceChangeType changeType) => LogHandlingSourceChange(this.logger, sourceId, changeType);

    [LoggerMessage(
        EventId = 45,
        Level = LogLevel.Information,
        Message = "Reloading {ServiceCount} impacted services for {SectionCount} sections")]
    private static partial void LogReloadingImpactedServices(ILogger logger, int serviceCount, int sectionCount);

    private void LogReloadingImpactedServices(int serviceCount, int sectionCount) => LogReloadingImpactedServices(this.logger, serviceCount, sectionCount);

    [LoggerMessage(
        EventId = 46,
        Level = LogLevel.Debug,
        Message = "Updated service {ServiceType} with new settings")]
    private static partial void LogUpdatedService(ILogger logger, string serviceType);

    [Conditional("DEBUG")]
    private void LogUpdatedService(string serviceType) => LogUpdatedService(this.logger, serviceType);

    [LoggerMessage(
        EventId = 47,
        Level = LogLevel.Error,
        Message = "Error handling source change event for {SourceId}")]
    private static partial void LogErrorHandlingSourceChange(ILogger logger, Exception exception, string sourceId);

    private void LogErrorHandlingSourceChange(Exception exception, string sourceId) => LogErrorHandlingSourceChange(this.logger, exception, sourceId);

    [LoggerMessage(
        EventId = 48,
        Level = LogLevel.Information,
        Message = "Restored section {SectionName} from earlier source {SourceId}")]
    private static partial void LogSectionRestored(ILogger logger, string sectionName, string sourceId);

    private void LogSectionRestored(string sectionName, string sourceId) => LogSectionRestored(this.logger, sectionName, sourceId);
}
