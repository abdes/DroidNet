// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.Assets;

/// <summary>Coalesces catalog notifications and prevents superseded scans from publishing.</summary>
public sealed partial class AssetFeed
{
    private readonly Lock refreshSync = new();
    private TaskCompletionSource? refreshCompletion;
    private Task? refreshTask;
    [SuppressMessage("Usage", "CA2213:Disposable fields should be disposed", Justification = "Non-owning alias; the scan's using scope disposes this source after its readers drain.")]
    private CancellationTokenSource? scanCancellation;
    private long catalogRevision;
    private long publishedRevision = -1;
    private ProjectContext? publishedProject;

    private void OnFreshnessChanged(object? sender, EventArgs args) => this.OnCatalogChanged();

    // The catalog would see the change only when its file watcher reports it; reading again now shows it at once.
    private void OnEditorFilesChanged(object? sender, AssetFilesChangedEventArgs args) => this.OnCatalogChanged();

    private Task RequestRefresh(bool invalidate)
    {
        lock (this.refreshSync)
        {
            ObjectDisposedException.ThrowIf(this.disposed, this);
            if (invalidate)
            {
                this.catalogRevision++;
            }

            if (this.refreshCompletion is null)
            {
                if (this.publishedRevision == this.catalogRevision
                    && ReferenceEquals(this.publishedProject, this.SessionProject))
                {
                    return Task.CompletedTask;
                }

                var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                this.refreshCompletion = completion;
                this.refreshTask = Task.Run(() => this.RefreshLoopAsync(completion), CancellationToken.None);
                _ = this.ObserveRefreshAsync(completion.Task);
            }

            return this.refreshCompletion.Task;
        }
    }

    private void OnCatalogChanged()
    {
        lock (this.refreshSync)
        {
            if (!this.disposed)
            {
                _ = this.RequestRefresh(invalidate: true);
            }
        }
    }

    // A change of the session's content mounts lists the project's assets again.
    private void OnContextChanged(object? sender, EventArgs args) => this.ResetProject();

    // The rows of the earlier mounts, or of an ended session, are dropped before the project is listed again.
    private void ResetProject()
    {
        lock (this.refreshSync)
        {
            if (this.disposed)
            {
                return;
            }

            this.scanCancellation?.Cancel();
            this.items.OnNext([]);
            _ = this.RequestRefresh(invalidate: true);
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Every failure must complete the shared promise; callers observe the fault and the background observer logs it.")]
    private async Task RefreshLoopAsync(TaskCompletionSource completion)
    {
        try
        {
            while (true)
            {
                using var cancellation = new CancellationTokenSource();
                long revision;
                lock (this.refreshSync)
                {
                    ObjectDisposedException.ThrowIf(this.disposed, this);
                    revision = this.catalogRevision;
                    this.scanCancellation = cancellation;
                }

                var project = this.SessionProject;
                IReadOnlyList<AssetEntry> snapshot = [];
                try
                {
                    if (project is not null)
                    {
                        await this.catalog.RefreshAsync(cancellation.Token).ConfigureAwait(false);
                        using var captured = await this.catalog.ReadSnapshotAsync(new AssetQuery(AssetQueryScope.All), cancellation.Token).ConfigureAwait(false);
                        var scope = this.projectCookScopeProvider.CreateScope(project);
                        var reduced = this.reducer.Reduce(captured.Records, project, scope);
                        snapshot = await this.ApplyCookStatusAsync(project, reduced, cancellation.Token, captured.Publication, captured.PublicationError).ConfigureAwait(false);
                    }
                }
                catch (OperationCanceledException) when (cancellation.IsCancellationRequested)
                {
                    // A project change invalidates this scan; the loop reads the new project.
                }
                finally
                {
                    lock (this.refreshSync)
                    {
                        this.scanCancellation = null;
                    }
                }

                if (this.TryPublishSnapshot(revision, project, snapshot, completion))
                {
                    return;
                }
            }
        }
        catch (Exception exception)
        {
            lock (this.refreshSync)
            {
                this.scanCancellation = null;
                if (ReferenceEquals(this.refreshCompletion, completion))
                {
                    this.refreshCompletion = null;
                }
            }

            _ = completion.TrySetException(exception);
        }
    }

    private bool TryPublishSnapshot(long revision, ProjectContext? project, IReadOnlyList<AssetEntry> snapshot, TaskCompletionSource completion)
    {
        lock (this.refreshSync)
        {
            if (this.disposed)
            {
                _ = completion.TrySetCanceled(new CancellationToken(canceled: true));
                return true;
            }

            if (revision != this.catalogRevision || !ReferenceEquals(project, this.SessionProject))
            {
                return false;
            }

            this.refreshCompletion = null;
            this.publishedRevision = revision;
            this.publishedProject = project;
            this.items.OnNext(this.ApplyLiveState(snapshot));
            _ = completion.TrySetResult();
            return true;
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Catalog notifications have no awaiting caller; preserve the shared stream and log every background refresh failure.")]
    private async Task ObserveRefreshAsync(Task refresh)
    {
        try
        {
            await refresh.ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            // Workspace disposal cancels background refresh observers.
        }
        catch (Exception exception)
        {
            this.LogRefreshFailed(exception);
        }
    }
}
