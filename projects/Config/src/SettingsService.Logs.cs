// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#pragma warning disable SA1204 // Static elements should appear before instance elements

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Config;

[System.Diagnostics.CodeAnalysis.SuppressMessage("StyleCop.CSharp.DocumentationRules", "SA1601:Partial elements should be documented", Justification = "Documented in the main part")]
public abstract partial class SettingsService<TSettings>
{
    [LoggerMessage(
    EventId = 101,
    Level = LogLevel.Debug,
    Message = "Saving settings for {SettingsType} in section {SectionName}")]
    static partial void LogSaving(ILogger logger, string settingsType, string sectionName);

    [Conditional("DEBUG")]
    private void LogSaving() => LogSaving(this.logger, typeof(TSettings).Name, this.SectionName);

    [LoggerMessage(
    EventId = 102,
    Level = LogLevel.Warning,
    Message = "Validation failed for {SettingsType}: {ErrorCount} errors")]
    static partial void LogValidationFailed(ILogger logger, string settingsType, int errorCount);

    private void LogValidationFailed(int errorCount) => LogValidationFailed(this.logger, typeof(TSettings).Name, errorCount);

    [LoggerMessage(
    EventId = 103,
    Level = LogLevel.Debug,
    Message = "Saved settings for {SettingsType} in section {SectionName}")]
    static partial void LogSavedSettings(ILogger logger, string settingsType, string sectionName);

    [Conditional("DEBUG")]
    private void LogSavedSettings() => LogSavedSettings(this.logger, typeof(TSettings).Name, this.SectionName);

    [LoggerMessage(
    EventId = 104,
    Level = LogLevel.Error,
    Message = "Exception saving settings for {SettingsType} in section {SectionName}")]
    static partial void LogExceptionSaving(ILogger logger, Exception exception, string settingsType, string sectionName);

    private void LogExceptionSaving(Exception exception) => LogExceptionSaving(this.logger, exception, typeof(TSettings).Name, this.SectionName);

    [LoggerMessage(
    EventId = 105,
    Level = LogLevel.Information,
    Message = "Reloading settings for {SettingsType} in section {SectionName}")]
    static partial void LogReloading(ILogger logger, string settingsType, string sectionName);

    private void LogReloading() => LogReloading(this.logger, typeof(TSettings).Name, this.SectionName);

    [LoggerMessage(
    EventId = 106,
    Level = LogLevel.Information,
    Message = "Reloaded settings for {SettingsType} in section {SectionName}")]
    static partial void LogReloaded(ILogger logger, string settingsType, string sectionName);

    private void LogReloaded() => LogReloaded(this.logger, typeof(TSettings).Name, this.SectionName);

    [LoggerMessage(
    EventId = 107,
    Level = LogLevel.Information,
    Message = "No settings found for {SettingsType} in section {SectionName}; using defaults")]
    static partial void LogUsingDefaults(ILogger logger, string settingsType, string sectionName);

    private void LogUsingDefaults() => LogUsingDefaults(this.logger, typeof(TSettings).Name, this.SectionName);

    [LoggerMessage(
    EventId = 108,
    Level = LogLevel.Information,
    Message = "Settings for {SettingsType} in section {SectionName} reset to defaults")]
    static partial void LogResetToDefaults(ILogger logger, string settingsType, string sectionName);

    private void LogResetToDefaults() => LogResetToDefaults(this.logger, typeof(TSettings).Name, this.SectionName);

    [LoggerMessage(
    EventId = 109,
    Level = LogLevel.Debug,
    Message = "Disposing SettingsService for {SettingsType}")]
    static partial void LogDisposingService(ILogger logger, string settingsType);

    [Conditional("DEBUG")]
    private void LogDisposingService() => LogDisposingService(this.logger, typeof(TSettings).Name);
}
