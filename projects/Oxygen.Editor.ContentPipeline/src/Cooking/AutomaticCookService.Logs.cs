// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>
///     Logging helpers for <see cref="AutomaticCookService"/>.
/// </summary>
public sealed partial class AutomaticCookService
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Automatic cooking failed for {Count} changed sources.")]
    private static partial void LogSchedulingFailure(ILogger logger, Exception exception, int count);

    private void LogSchedulingFailure(Exception exception, int count)
        => LogSchedulingFailure(this.logger, exception, count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Cook on change stopped scheduling; the next change starts it again.")]
    private static partial void LogDrainFailure(ILogger logger, Exception exception);

    private void LogDrainFailure(Exception exception)
        => LogDrainFailure(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "{Count} changed sources were not cooked: they, or sources they use, have errors ({Sources}); the change that fixes them cooks them.")]
    private static partial void LogSkippedForErrors(ILogger logger, int count, string sources);

    private void LogSkippedForErrors(List<SkippedCookSource> skipped)
    {
        if (!this.logger.IsEnabled(LogLevel.Information))
        {
            return;
        }

        var count = skipped.Count;
        var sources = string.Join(", ", skipped.Select(static source => source.Source.VirtualPath));
        LogSkippedForErrors(this.logger, count, sources);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Cook on change is off; {Count} changed sources were not cooked.")]
    private static partial void LogNotCooked(ILogger logger, int count);

    private void LogNotCooked(int count)
        => LogNotCooked(this.logger, count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "{Count} changed sources were not cooked again: their published output already matches the files.")]
    private static partial void LogAlreadyCooked(ILogger logger, int count);

    private void LogAlreadyCooked(int count)
        => LogAlreadyCooked(this.logger, count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "{Count} changed sources were not cooked: their files are gone and they had no cooked output to remove, or they cannot be cook inputs.")]
    private static partial void LogSourcesGone(ILogger logger, int count);

    private void LogSourcesGone(int count)
        => LogSourcesGone(this.logger, count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The cook status of {Count} changed sources could not be read; they are cooked.")]
    private static partial void LogStatusUnavailable(ILogger logger, Exception exception, int count);

    private void LogStatusUnavailable(Exception exception, int count)
        => LogStatusUnavailable(this.logger, exception, count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Checking {Count} changed sources for cook on change: {Sources}.")]
    private static partial void LogCheckingChangedSources(ILogger logger, int count, string sources);

    private void LogCheckingChangedSources(List<AssetPath> changedSources)
    {
        if (!this.logger.IsEnabled(LogLevel.Information))
        {
            return;
        }

        var count = changedSources.Count;
        var sources = string.Join(", ", changedSources.Select(static uri => uri.VirtualPath));
        LogCheckingChangedSources(this.logger, count, sources);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Waiting for the cook, import or relocation in progress to finish before checking {Count} changed sources.")]
    private static partial void LogWaitingForWriter(ILogger logger, int count);

    private void LogWaitingForWriter(int count)
        => LogWaitingForWriter(this.logger, count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "The project writer became idle after {Seconds:0.0} s; checking the changed sources.")]
    private static partial void LogWriterIdle(ILogger logger, double seconds);

    private void LogWriterIdle(long waitStarted)
    {
        if (!this.logger.IsEnabled(LogLevel.Information))
        {
            return;
        }

        var seconds = this.time.GetElapsedTime(waitStarted).TotalSeconds;
        LogWriterIdle(this.logger, seconds);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Change of '{Uri}' is not a cookable source under an authoring mount; cook on change ignores it.")]
    private static partial void LogChangeIgnored(ILogger logger, AssetPath uri);

    [Conditional("DEBUG")]
    private void LogChangeIgnored(AssetPath uri)
        => LogChangeIgnored(this.logger, uri);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Cook on change set to {Enabled} for the active project.")]
    private static partial void LogCookOnChangeSet(ILogger logger, bool enabled);

    private void LogCookOnChangeSet(bool enabled)
        => LogCookOnChangeSet(this.logger, enabled);
}
