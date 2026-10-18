// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
///     Logging helpers for <see cref="ContentCookCoordinator"/>.
/// </summary>
public sealed partial class ContentCookCoordinator
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Content cook lifetime cancellation failed.")]
    private static partial void LogLifetimeFailure(ILogger logger, Exception exception);

    private void LogLifetimeFailure(Exception exception)
        => LogLifetimeFailure(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "A cooking presentation subscriber failed.")]
    private static partial void LogRunObserverFailure(ILogger logger, Exception exception);

    private void LogRunObserverFailure(Exception exception)
        => LogRunObserverFailure(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Cook of {Name} queued for {Origin}.")]
    private static partial void LogRunQueued(ILogger logger, string name, string origin);

    private void LogRunQueued(CookRunSnapshot run)
    {
        if (!this.logger.IsEnabled(LogLevel.Information))
        {
            return;
        }

        var name = DescribeRun(run);
        var origin = run.Request.IsDemand ? "the active preview" : run.Request.IsAutomatic ? "cook on change" : "a request";
        LogRunQueued(this.logger, name, origin);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Cook of {Name} started.")]
    private static partial void LogRunStarted(ILogger logger, string name);

    private void LogRunStarted(Guid operationId)
    {
        if (!this.logger.IsEnabled(LogLevel.Information))
        {
            return;
        }

        CookRunSnapshot? run;
        lock (this.stateLock)
        {
            run = this.runs.TryGetValue(operationId, out var progress) ? progress.Snapshot : null;
        }

        if (run is not null)
        {
            var name = DescribeRun(run);
            LogRunStarted(this.logger, name);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Cook of {Name} waits for {Count} documents to be saved.")]
    private static partial void LogRunWaitingForSave(ILogger logger, string name, int count);

    private void LogRunWaitingForSave(CookRunSnapshot run)
    {
        if (!this.logger.IsEnabled(LogLevel.Information))
        {
            return;
        }

        var name = DescribeRun(run);
        var count = run.UnsavedDocuments.Length;
        LogRunWaitingForSave(this.logger, name, count);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Cook of {Scope} joined the queued cook of {Name}.")]
    private static partial void LogRunJoined(ILogger logger, string scope, string name);

    private void LogRunJoined(CookRunRequest request, CookRunSnapshot joined)
    {
        if (!this.logger.IsEnabled(LogLevel.Information))
        {
            return;
        }

        var scope = DescribeScope(request);
        var name = DescribeRun(joined);
        LogRunJoined(this.logger, scope, name);
    }

    // A run's finish takes one message by its state; each message's wrapper gathers its own arguments.
    private void LogRunFinished(CookRunSnapshot run, bool stopped, string? stoppedFor)
    {
        switch (run.State)
        {
            case CookRunState.Succeeded or CookRunState.UpToDate when run.Diagnostics.Any(IsRetiredOutput):
                this.LogRunRemovedOutput(run);
                break;
            case CookRunState.Succeeded or CookRunState.UpToDate:
                this.LogRunSucceeded(run);
                break;
            case CookRunState.SucceededWithWarnings:
                this.LogRunWarned(run);
                break;
            case CookRunState.Cancelled when stopped:
                this.LogRunStopped(run, stoppedFor);
                break;
            case CookRunState.Cancelled:
                this.LogRunCancelled(run);
                break;
            default:
                this.LogRunFailed(run);
                break;
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Cook of {Name} removed the cooked output of {Sources}, whose sources were moved or deleted, in {Seconds:F1} s; it cooked {Updated} assets and reused {Reused}.")]
    private static partial void LogRunRemovedOutput(ILogger logger, string name, string sources, double seconds, int updated, int reused);

    private void LogRunRemovedOutput(CookRunSnapshot run)
    {
        if (!this.logger.IsEnabled(LogLevel.Information))
        {
            return;
        }

        var name = DescribeRun(run);
        var sources = string.Join(", ", run.Diagnostics.Where(IsRetiredOutput).Select(static issue => issue.AffectedVirtualPath).OfType<string>());
        var seconds = Elapsed(run);
        var updated = Count(run, CookAssetState.Updated);
        var reused = Count(run, CookAssetState.Reused);
        LogRunRemovedOutput(this.logger, name, sources, seconds, updated, reused);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Cook of {Name} finished ({State}) in {Seconds:F1} s: {Updated} assets cooked, {Reused} reused.")]
    private static partial void LogRunSucceeded(ILogger logger, string name, CookRunState state, double seconds, int updated, int reused);

    private void LogRunSucceeded(CookRunSnapshot run)
    {
        if (!this.logger.IsEnabled(LogLevel.Information))
        {
            return;
        }

        var name = DescribeRun(run);
        var seconds = Elapsed(run);
        var updated = Count(run, CookAssetState.Updated);
        var reused = Count(run, CookAssetState.Reused);
        LogRunSucceeded(this.logger, name, run.State, seconds, updated, reused);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Cook of {Name} finished with warnings in {Seconds:F1} s: {Updated} assets cooked, {Reused} reused. {Warning}")]
    private static partial void LogRunWarned(ILogger logger, string name, double seconds, int updated, int reused, string? warning);

    private void LogRunWarned(CookRunSnapshot run)
    {
        if (!this.logger.IsEnabled(LogLevel.Warning))
        {
            return;
        }

        var name = DescribeRun(run);
        var seconds = Elapsed(run);
        var updated = Count(run, CookAssetState.Updated);
        var reused = Count(run, CookAssetState.Reused);
        var warning = run.Diagnostics.FirstOrDefault(static issue => issue.Severity == DiagnosticSeverity.Warning)?.Message;
        LogRunWarned(this.logger, name, seconds, updated, reused, warning);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Cook of {Name} cancelled after {Seconds:F1} s.")]
    private static partial void LogRunCancelled(ILogger logger, string name, double seconds);

    private void LogRunCancelled(CookRunSnapshot run)
    {
        if (!this.logger.IsEnabled(LogLevel.Information))
        {
            return;
        }

        var name = DescribeRun(run);
        var seconds = Elapsed(run);
        LogRunCancelled(this.logger, name, seconds);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Cook of {Name} stopped after {Seconds:F1} s so that {Work} could run first.")]
    private static partial void LogRunStopped(ILogger logger, string name, double seconds, string work);

    private void LogRunStopped(CookRunSnapshot run, string? stoppedFor)
    {
        if (!this.logger.IsEnabled(LogLevel.Information))
        {
            return;
        }

        var name = DescribeRun(run);
        var seconds = Elapsed(run);
        LogRunStopped(this.logger, name, seconds, stoppedFor ?? OtherWriterWork);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Cook of {Name} failed after {Seconds:F1} s: {Error}")]
    private static partial void LogRunFailed(ILogger logger, string name, double seconds, string? error);

    private void LogRunFailed(CookRunSnapshot run)
    {
        if (!this.logger.IsEnabled(LogLevel.Error))
        {
            return;
        }

        var name = DescribeRun(run);
        var seconds = Elapsed(run);
        var error = run.Diagnostics.FirstOrDefault(static issue => issue.Severity is DiagnosticSeverity.Error or DiagnosticSeverity.Fatal)?.Message;
        LogRunFailed(this.logger, name, seconds, error);
    }

    private static bool IsRetiredOutput(DiagnosticRecord issue)
        => string.Equals(issue.Code, ContentPipelineService.OutputRetiredCode, StringComparison.Ordinal);

    private static double Elapsed(CookRunSnapshot run)
        => ((run.CompletedAt ?? DateTimeOffset.UtcNow) - run.StartedAt).TotalSeconds;

    private static int Count(CookRunSnapshot run, CookAssetState state)
        => run.Assets.Values.Count(asset => asset.State == state);
}
