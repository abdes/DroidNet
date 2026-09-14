// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Retains scoped cooking results and progress for the current editor session.</summary>
public sealed partial class ContentCookCoordinator
{
    private readonly Dictionary<Guid, RunEntry> runs = [];
    private readonly HashSet<Guid> currentQueue = [];
    private bool revealQueueOutcome;

    /// <inheritdoc />
    public event EventHandler<CookRunChangedEventArgs>? RunChanged;

    /// <inheritdoc />
    public event CookCompletedHandler? CookCompleted;

    /// <inheritdoc />
    public IReadOnlyList<CookRunSnapshot> Runs
    {
        get
        {
            lock (this.stateLock)
            {
                return this.runs.Values.Select(static entry => entry.Snapshot).ToArray();
            }
        }
    }

    /// <inheritdoc />
    public Task CancelAsync(Guid operationId)
    {
        Task cancelled;
        CookRunSnapshot snapshot;
        lock (this.stateLock)
        {
            if (!this.runs.TryGetValue(operationId, out var run) || run.Snapshot.IsCompleted)
            {
                return Task.CompletedTask;
            }

            run.Snapshot = snapshot = Append(run.Snapshot, new(Message: "Cancellation requested.", State: CookRunState.Cancelling));
            cancelled = run.Cancellation.CancelAsync();
        }

        this.PublishRun(snapshot);
        return cancelled;
    }

    /// <inheritdoc />
    public bool ResumeAfterSave(Guid operationId)
    {
        CookRunSnapshot snapshot;
        TaskCompletionSource resume;
        lock (this.stateLock)
        {
            if (!this.runs.TryGetValue(operationId, out var run)
                || run.Snapshot.State != CookRunState.NeedsSave || run.Resume is null)
            {
                return false;
            }

            resume = run.Resume;
            run.Resume = null;
            run.Snapshot = snapshot = Append(run.Snapshot with { UnsavedDocuments = [] }, new(Message: "Rechecking saved inputs.", State: CookRunState.Queued));
        }

        this.PublishRun(snapshot);
        _ = resume.TrySetResult();
        return true;
    }

    private static CookRunSnapshot Append(CookRunSnapshot snapshot, CookRunProgress progress)
    {
        if (progress.RecoveryRequest is { } recovery)
        {
            var assets = snapshot.Request.ScopeUri is { } prior && prior != recovery.ScopeUri ? snapshot.Assets.Remove(prior) : snapshot.Assets;
            snapshot = snapshot with { Request = recovery, Assets = assets };
        }

        var messages = snapshot.Messages;
        if (!string.IsNullOrWhiteSpace(progress.Message))
        {
            messages = messages.Add(new(messages.Count + 1L, DateTimeOffset.UtcNow, progress.Severity, progress.Message, progress.Asset?.AssetUri));
        }

        return snapshot with
        {
            Revision = snapshot.Revision + 1,
            State = snapshot.State == CookRunState.Cancelling ? snapshot.State : progress.State ?? snapshot.State,
            Messages = messages,
            Assets = progress.Asset is { } asset ? snapshot.Assets.SetItem(asset.AssetUri, asset) : snapshot.Assets,
            Diagnostics = progress.Diagnostic is { } diagnostic ? snapshot.Diagnostics.Add(diagnostic) : snapshot.Diagnostics,
        };
    }

    private static IEnumerable<CookRunAsset> FinishedAssets(IEnumerable<ContentCookedAsset> cooked, CookAssetState state)
    {
        var sources = new HashSet<Uri>();
        foreach (var asset in cooked)
        {
            var imported = IsModelSource(asset.SourceAssetUri);
            yield return new(imported ? asset.CookedAssetUri : asset.SourceAssetUri, asset.Kind, state);
            if (imported && sources.Add(asset.SourceAssetUri))
            {
                yield return new(asset.SourceAssetUri, ContentCookAssetKind.ForeignSource, state);
            }
        }
    }

    private static bool IsModelSource(Uri uri) => Path.GetExtension(uri.AbsolutePath).ToUpperInvariant() is ".GLTF" or ".GLB" or ".FBX";

    private static string GetScopeName(ContentCookOperation operation, CookRunRequest request)
    {
        if (request.Import is { } import)
        {
            return import.Name;
        }

        if (request.TargetKind == CookTargetKind.Project || request.ScopeUri is null)
        {
            return operation.Project.Name;
        }

        var name = Uri.UnescapeDataString(request.ScopeUri.AbsolutePath).TrimEnd('/');
        name = name[(name.LastIndexOf('/') + 1)..];
        return request.TargetKind == CookTargetKind.Folder ? name : Path.GetFileNameWithoutExtension(Path.GetFileNameWithoutExtension(name));
    }

    private Task BlockRun(Guid operationId, ImmutableArray<CookDocumentState> documents)
    {
        CookRunSnapshot snapshot;
        Task resume;
        lock (this.stateLock)
        {
            var run = this.runs[operationId];
            run.Resume = new(TaskCreationOptions.RunContinuationsAsynchronously);
            resume = run.Resume.Task;
            run.Snapshot = snapshot = Append(run.Snapshot with { UnsavedDocuments = documents }, new(Message: "Waiting for participating documents to be saved.", State: CookRunState.NeedsSave));
        }

        this.PublishRun(snapshot, reveal: !snapshot.Request.IsAutomatic);
        return resume;
    }

    private RunProgress AddRun(ContentCookOperation operation, CookRunRequest request, CancellationTokenSource cancellation, SharedCook? shared)
    {
        var snapshot = new CookRunSnapshot
        {
            OperationId = operation.OperationId,
            ProjectId = operation.Project.ProjectId,
            ProjectRoot = operation.Project.ProjectRoot,
            DisplayName = GetScopeName(operation, request),
            Request = request,
        };
        if (request.ScopeUri is { } assetUri && request.TargetKind is CookTargetKind.Asset or CookTargetKind.CurrentScene)
        {
            var kind = request.TargetKind == CookTargetKind.CurrentScene || assetUri.AbsolutePath.EndsWith(".oscene.json", StringComparison.OrdinalIgnoreCase)
                ? ContentCookAssetKind.Scene : assetUri.AbsolutePath.EndsWith(".omat.json", StringComparison.OrdinalIgnoreCase)
                    ? ContentCookAssetKind.Material : assetUri.AbsolutePath.EndsWith(".ogeo.json", StringComparison.OrdinalIgnoreCase)
                        ? ContentCookAssetKind.Geometry : request.Import is not null || request.IsReimport || IsModelSource(assetUri) ? ContentCookAssetKind.ForeignSource : ContentCookAssetKind.Unknown;
            snapshot = snapshot with { Assets = snapshot.Assets.Add(assetUri, new(assetUri, kind, CookAssetState.Preparing)) };
        }

        snapshot = Append(snapshot, new(Message: request.IsDemand ? "Queued for the active preview."
            : request.IsAutomatic ? "Queued after saving." : "Cook queued."));
        lock (this.stateLock)
        {
            snapshot = snapshot with { Request = shared?.Request ?? request };
            this.runs.Add(operation.OperationId, new(snapshot, cancellation));
            _ = this.currentQueue.Add(operation.OperationId);
            this.revealQueueOutcome |= !snapshot.Request.IsAutomatic;
        }

        this.PublishRun(snapshot, reveal: !snapshot.Request.IsAutomatic);
        return new RunProgress(this, operation.OperationId);
    }

    private void ReportRun(Guid operationId, CookRunProgress progress)
    {
        CookRunSnapshot snapshot;
        lock (this.stateLock)
        {
            if (!this.runs.TryGetValue(operationId, out var run) || run.Snapshot.IsCompleted)
            {
                return;
            }

            run.Snapshot = snapshot = Append(run.Snapshot, progress);
        }

        this.PublishRun(snapshot);
    }

    private async Task CompleteRunAsync<T>(ContentCookOperation operation, T result, CancellationToken cancellationToken)
    {
        var state = CookRunState.Succeeded;
        IEnumerable<DiagnosticRecord> diagnostics = [];
        IEnumerable<CookRunAsset> assets = [];
        if (result is ContentCookResult content)
        {
            state = content.IsUpToDate && content.Status == OperationStatus.Succeeded ? CookRunState.UpToDate : content.Status switch
            {
                OperationStatus.Succeeded => CookRunState.Succeeded,
                OperationStatus.SucceededWithWarnings => CookRunState.SucceededWithWarnings,
                OperationStatus.Cancelled => CookRunState.Cancelled,
                _ => CookRunState.Failed,
            };
            diagnostics = content.Diagnostics;
            assets = FinishedAssets(content.CookedAssets, CookAssetState.Updated)
                .Concat(FinishedAssets(content.ReusedAssets, CookAssetState.Reused));
        }
        else if (result is MaterialCookResult material)
        {
            state = material.State == MaterialCookState.Cooked ? CookRunState.Succeeded : CookRunState.Failed;
        }

        if (result is ContentCookResult completed)
        {
            if (this.CookCompleted is not null && completed.Validation is { Succeeded: true })
            {
                this.ReportRun(operation.OperationId, new(Message: "Refreshing cooked content.", State: CookRunState.Publishing));
            }

            await this.PublishCookCompletedAsync(new(operation.Project, completed)).ConfigureAwait(false);
        }

        if (result is not ContentCookResult { IsPublished: true })
        {
            cancellationToken.ThrowIfCancellationRequested();
        }

        this.VerifyCurrent(operation);
        this.FinishRun(operation.OperationId, state, diagnostics, assets);
    }

    private void FailRun(Guid operationId, Exception exception)
    {
        var state = exception is OperationCanceledException ? CookRunState.Cancelled : CookRunState.Failed;
        var diagnostics = exception is NativeCompatibilityException compatibility
            ? compatibility.Diagnostics.Select(diagnostic => diagnostic with { OperationId = operationId }).ToArray()
            : state == CookRunState.Cancelled ? Array.Empty<DiagnosticRecord>() :
        [
            new DiagnosticRecord
            {
                OperationId = operationId,
                Domain = FailureDomain.ContentPipeline,
                Severity = DiagnosticSeverity.Error,
                Code = AssetCookDiagnosticCodes.CookFailed,
                Message = exception.Message,
                ExceptionType = exception.GetType().FullName,
            },
        ];
        this.FinishRun(operationId, state, diagnostics, []);
    }

    private void FinishRun(Guid operationId, CookRunState state, IEnumerable<DiagnosticRecord> diagnostics, IEnumerable<CookRunAsset> assets)
    {
        CookRunSnapshot snapshot;
        CookRunSnapshot? queueOutcome = null;
        lock (this.stateLock)
        {
            if (!this.runs.TryGetValue(operationId, out var run) || run.Snapshot.IsCompleted)
            {
                return;
            }

            snapshot = run.Snapshot;
            foreach (var diagnostic in diagnostics)
            {
                if (snapshot.Diagnostics.Any(existing => existing.DiagnosticId == diagnostic.DiagnosticId))
                {
                    continue;
                }

                snapshot = Append(snapshot, new(diagnostic.Message, diagnostic.Severity, Diagnostic: diagnostic));
                if (!string.IsNullOrWhiteSpace(diagnostic.TechnicalMessage))
                {
                    snapshot = Append(snapshot, new(diagnostic.TechnicalMessage, diagnostic.Severity));
                }
            }

            foreach (var asset in assets)
            {
                snapshot = Append(snapshot, new(Asset: asset));
            }

            foreach (var asset in snapshot.Assets.Values.Where(static asset => asset.State is CookAssetState.Preparing or CookAssetState.Cooking))
            {
                var issue = snapshot.Diagnostics.FirstOrDefault(diagnostic => diagnostic.Severity is DiagnosticSeverity.Error or DiagnosticSeverity.Fatal
                    && string.Equals(diagnostic.AffectedVirtualPath, asset.AssetUri.AbsolutePath, StringComparison.Ordinal));
                var assetState = issue is not null ? CookAssetState.Failed
                    : state == CookRunState.Cancelled ? CookAssetState.Cancelled
                    : asset.State == CookAssetState.Preparing ? CookAssetState.Skipped : CookAssetState.Unresolved;
                snapshot = Append(snapshot, new(Asset: asset with
                {
                    State = assetState,
                    Reason = issue?.Message ?? (assetState == CookAssetState.Skipped ? "Preparation stopped before this asset was cooked." : "No individual completion was reported. See the cook output."),
                }));
            }

            var message = state switch
            {
                CookRunState.Succeeded => "Cook complete.",
                CookRunState.SucceededWithWarnings => "Cook completed with warnings.",
                CookRunState.UpToDate => "Already up to date.",
                CookRunState.Cancelled => "Cook cancelled. Owned work has stopped.",
                _ => "Cook failed.",
            };
            snapshot = Append(snapshot, new(message));
            run.Snapshot = snapshot = snapshot with { State = state, CompletedAt = DateTimeOffset.UtcNow };
            queueOutcome = this.TakeCompletedQueueOutcome();
        }

        this.PublishCompletedQueue(snapshot, queueOutcome);
    }

    private void PublishCompletedQueue(CookRunSnapshot snapshot, CookRunSnapshot? queueOutcome)
    {
        this.PublishRun(snapshot, reveal: queueOutcome?.OperationId == snapshot.OperationId);
        if (queueOutcome is not null && queueOutcome.OperationId != snapshot.OperationId)
        {
            this.PublishRun(queueOutcome, reveal: true);
        }
    }

    private CookRunSnapshot? TakeCompletedQueueOutcome()
    {
        if (this.currentQueue.Any(id => !this.runs[id].Snapshot.IsCompleted))
        {
            return null;
        }

        var outcome = this.revealQueueOutcome
            ? this.currentQueue.Select(id => this.runs[id].Snapshot)
                .OrderBy(static item => item.State == CookRunState.Failed ? 0 : item.State == CookRunState.SucceededWithWarnings ? 1 : 2)
                .ThenByDescending(static item => item.CompletedAt).First()
            : null;
        this.currentQueue.Clear();
        this.revealQueueOutcome = false;
        return outcome;
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A presentation subscriber must not interrupt cook ownership, worker drain, or publication.")]
    private void PublishRun(CookRunSnapshot snapshot, bool reveal = false)
    {
        foreach (var handler in this.RunChanged?.GetInvocationList() ?? [])
        {
            try
            {
                ((EventHandler<CookRunChangedEventArgs>)handler)(this, new(snapshot, reveal));
            }
            catch (Exception ex)
            {
                this.LogRunObserverFailure(ex);
            }
        }
    }

    private async Task PublishCookCompletedAsync(CookCompletedEventArgs args)
    {
        foreach (var handler in this.CookCompleted?.GetInvocationList() ?? [])
        {
            await ((CookCompletedHandler)handler)(args).ConfigureAwait(false);
        }
    }

    [LoggerMessage(Level = LogLevel.Error, Message = "A cooking presentation subscriber failed.")]
    private partial void LogRunObserverFailure(Exception exception);

    private sealed class RunEntry(CookRunSnapshot snapshot, CancellationTokenSource cancellation)
    {
        internal CookRunSnapshot Snapshot { get; set; } = snapshot;

        internal CancellationTokenSource Cancellation { get; } = cancellation;

        internal TaskCompletionSource? Resume { get; set; }
    }

    private sealed class RunProgress(ContentCookCoordinator owner, Guid operationId) : IProgress<CookRunProgress>
    {
        public void Report(CookRunProgress value) => owner.ReportRun(operationId, value);
    }
}
