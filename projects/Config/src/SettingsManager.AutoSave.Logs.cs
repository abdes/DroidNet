// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Config;

/// <summary>
///     Provides a last-loaded-wins implementation of <see cref="ISettingsManager"/> for multi-source settings composition.
/// </summary>
public sealed partial class SettingsManager
{
    [LoggerMessage(
        EventId = 60,
        Level = LogLevel.Information,
        Message = "AutoSave enabled and started")]
    private static partial void LogAutoSaveStarted(ILogger logger);

    private void LogAutoSaveStarted() => LogAutoSaveStarted(this.logger);

    [LoggerMessage(
        EventId = 61,
        Level = LogLevel.Information,
        Message = "AutoSave disabled and stopped")]
    private static partial void LogAutoSaveStopped(ILogger logger);

    private void LogAutoSaveStopped() => LogAutoSaveStopped(this.logger);

    [LoggerMessage(
        EventId = 62,
        Level = LogLevel.Debug,
        Message = "AutoSave debouncing triggered, delay: {Delay}")]
    private static partial void LogAutoSaveDebouncing(ILogger logger, TimeSpan delay);

    [Conditional("DEBUG")]
    private void LogAutoSaveDebouncing(TimeSpan delay) => LogAutoSaveDebouncing(this.logger, delay);

    [LoggerMessage(
        EventId = 63,
        Level = LogLevel.Information,
        Message = "AutoSave saving {Count} dirty services")]
    private static partial void LogAutoSavingServices(ILogger logger, int count);

    private void LogAutoSavingServices(int count) => LogAutoSavingServices(this.logger, count);

    [LoggerMessage(
        EventId = 64,
        Level = LogLevel.Debug,
        Message = "AutoSave saved service: {SectionName}")]
    private static partial void LogAutoSavedService(ILogger logger, string sectionName);

    [Conditional("DEBUG")]
    private void LogAutoSavedService(string sectionName) => LogAutoSavedService(this.logger, sectionName);

    [LoggerMessage(
        EventId = 65,
        Level = LogLevel.Warning,
        Message = "AutoSave failed to save service {SectionName}")]
    private static partial void LogAutoSaveServiceFailed(ILogger logger, string sectionName, Exception exception);

    private void LogAutoSaveServiceFailed(string sectionName, Exception exception) => LogAutoSaveServiceFailed(this.logger, sectionName, exception);

    [LoggerMessage(
        EventId = 66,
        Level = LogLevel.Information,
        Message = "AutoSave completed for {Count} services")]
    private static partial void LogAutoSaveCompleted(ILogger logger, int count);

    private void LogAutoSaveCompleted(int count) => LogAutoSaveCompleted(this.logger, count);

    [LoggerMessage(
        EventId = 67,
        Level = LogLevel.Debug,
        Message = "AutoSave operation pending - will execute after current operation completes")]
    private static partial void LogAutoSavePending(ILogger logger);

    [Conditional("DEBUG")]
    private void LogAutoSavePending() => LogAutoSavePending(this.logger);

    [LoggerMessage(
        EventId = 68,
        Level = LogLevel.Information,
        Message = "AutoSave operation cancelled")]
    private static partial void LogAutoSaveCancelled(ILogger logger);

    private void LogAutoSaveCancelled() => LogAutoSaveCancelled(this.logger);

    [LoggerMessage(
        EventId = 69,
        Level = LogLevel.Error,
        Message = "AutoSave error occurred")]
    private static partial void LogAutoSaveError(ILogger logger, Exception exception);

    private void LogAutoSaveError(Exception exception) => LogAutoSaveError(this.logger, exception);

    [LoggerMessage(
        EventId = 70,
        Level = LogLevel.Warning,
        Message = "AutoSave validation failed for service {SectionName} with {ErrorCount} errors")]
    private static partial void LogAutoSaveValidationFailed(ILogger logger, string sectionName, int errorCount);

    private void LogAutoSaveValidationFailed(string sectionName, int errorCount) => LogAutoSaveValidationFailed(this.logger, sectionName, errorCount);
}
