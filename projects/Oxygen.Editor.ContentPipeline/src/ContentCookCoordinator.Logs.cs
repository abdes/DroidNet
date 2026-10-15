// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <inheritdoc cref="ContentCookCoordinator"/>
public sealed partial class ContentCookCoordinator
{
    [LoggerMessage(Level = LogLevel.Error, Message = "Content cook lifetime cancellation failed.")]
    private partial void LogLifetimeFailure(Exception exception);

    [LoggerMessage(Level = LogLevel.Error, Message = "A cooking presentation subscriber failed.")]
    private partial void LogRunObserverFailure(Exception exception);

    [LoggerMessage(Level = LogLevel.Information, Message = "Cook of {Name} queued for {Origin}.")]
    private partial void LogRunQueued(string name, string origin);

    [LoggerMessage(Level = LogLevel.Information, Message = "Cook of {Name} started.")]
    private partial void LogRunStarted(string name);

    [LoggerMessage(Level = LogLevel.Information, Message = "Cook of {Name} waits for {Count} documents to be saved.")]
    private partial void LogRunWaitingForSave(string name, int count);

    [LoggerMessage(Level = LogLevel.Information, Message = "Cook of {Name} finished ({State}) in {Seconds:F1} s: {Updated} assets cooked, {Reused} reused.")]
    private partial void LogRunSucceeded(string name, CookRunState state, double seconds, int updated, int reused);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Cook of {Name} finished with warnings in {Seconds:F1} s: {Updated} assets cooked, {Reused} reused. {Warning}")]
    private partial void LogRunWarned(string name, double seconds, int updated, int reused, string? warning);

    [LoggerMessage(Level = LogLevel.Information, Message = "Cook of {Name} cancelled after {Seconds:F1} s.")]
    private partial void LogRunCancelled(string name, double seconds);

    [LoggerMessage(Level = LogLevel.Error, Message = "Cook of {Name} failed after {Seconds:F1} s: {Error}")]
    private partial void LogRunFailed(string name, double seconds, string? error);

    private void LogRunStarted(Guid operationId)
    {
        string? name;
        lock (this.stateLock)
        {
            name = this.runs.TryGetValue(operationId, out var run) ? run.Snapshot.DisplayName : null;
        }

        if (name is not null)
        {
            this.LogRunStarted(name);
        }
    }

    private void LogRunFinished(CookRunSnapshot run)
    {
        var seconds = ((run.CompletedAt ?? DateTimeOffset.UtcNow) - run.StartedAt).TotalSeconds;
        var updated = run.Assets.Values.Count(static asset => asset.State == CookAssetState.Updated);
        var reused = run.Assets.Values.Count(static asset => asset.State == CookAssetState.Reused);
        switch (run.State)
        {
            case CookRunState.Succeeded or CookRunState.UpToDate:
                this.LogRunSucceeded(run.DisplayName, run.State, seconds, updated, reused);
                break;
            case CookRunState.SucceededWithWarnings:
                this.LogRunWarned(run.DisplayName, seconds, updated, reused, run.Diagnostics.FirstOrDefault(static issue => issue.Severity == DiagnosticSeverity.Warning)?.Message);
                break;
            case CookRunState.Cancelled:
                this.LogRunCancelled(run.DisplayName, seconds);
                break;
            default:
                this.LogRunFailed(run.DisplayName, seconds, run.Diagnostics.FirstOrDefault(static issue => issue.Severity is DiagnosticSeverity.Error or DiagnosticSeverity.Fatal)?.Message);
                break;
        }
    }
}
