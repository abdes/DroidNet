// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <inheritdoc cref="AutomaticCookService"/>
public sealed partial class AutomaticCookService
{
    [LoggerMessage(Level = LogLevel.Error, Message = "Automatic cooking failed for changed source {Source}.")]
    private partial void LogSchedulingFailure(Exception exception, string source);

    [LoggerMessage(Level = LogLevel.Information, Message = "Cook on change is off; {Count} changed sources were not cooked.")]
    private partial void LogNotCooked(int count);

    [LoggerMessage(Level = LogLevel.Information, Message = "{Count} changed sources were not cooked on their own: each resumed a cook that was waiting for it to be saved.")]
    private partial void LogResumedWaitingRuns(int count);

    [LoggerMessage(Level = LogLevel.Information, Message = "{Count} changed sources were not cooked again: their published output already matches the files.")]
    private partial void LogAlreadyCooked(int count);

    [LoggerMessage(Level = LogLevel.Information, Message = "{Count} changed sources were not cooked: their files are gone and they had no cooked output to remove.")]
    private partial void LogSourcesGone(int count);

    [LoggerMessage(Level = LogLevel.Warning, Message = "The cook status of {Count} changed sources could not be read; they are cooked.")]
    private partial void LogStatusUnavailable(Exception exception, int count);

    [LoggerMessage(Level = LogLevel.Information, Message = "Checking {Count} changed sources for cook on change: {Sources}.")]
    private partial void LogCheckingChangedSourcesCore(int count, string sources);

    [LoggerMessage(Level = LogLevel.Information, Message = "Waiting for the cook, import or relocation in progress to finish before checking {Count} changed sources.")]
    private partial void LogWaitingForWriter(int count);

    [LoggerMessage(Level = LogLevel.Information, Message = "The project writer became idle after {Seconds:0.0} s; checking the changed sources.")]
    private partial void LogWriterIdle(double seconds);

    [LoggerMessage(SkipEnabledCheck = true, Level = LogLevel.Debug, Message = "Change of '{Uri}' is not a cookable source under an authoring mount; cook on change ignores it.")]
    private static partial void LogChangeIgnored(ILogger logger, Uri uri);

    [LoggerMessage(Level = LogLevel.Information, Message = "Cancelling the cook on change of '{Source}': the file was moved or deleted; cook on change checks it again.")]
    private partial void LogCookRetiredCore(string source);

    [LoggerMessage(Level = LogLevel.Information, Message = "Cook on change set to {Enabled} for the active project.")]
    private partial void LogCookOnChangeSet(bool enabled);

    private void LogCheckingChangedSources(int count, IEnumerable<Uri> sources)
    {
        if (this.logger.IsEnabled(LogLevel.Information))
        {
            this.LogCheckingChangedSourcesCore(count, string.Join(", ", sources.Select(static uri => Uri.UnescapeDataString(uri.AbsolutePath))));
        }
    }

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogChangeIgnored(Uri uri)
#pragma warning disable CA1873 // Compiled out of release builds by Conditional("DEBUG")
        => LogChangeIgnored(this.logger, uri);
#pragma warning restore CA1873

    private void LogCookRetired(Uri source)
    {
        if (this.logger.IsEnabled(LogLevel.Information))
        {
            this.LogCookRetiredCore(Uri.UnescapeDataString(source.AbsolutePath));
        }
    }
}
