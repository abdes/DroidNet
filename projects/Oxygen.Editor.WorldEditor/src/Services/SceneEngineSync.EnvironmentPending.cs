// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Services;

/// <summary>Keeps environment publication ordered without converting scene systems to scalar properties.</summary>
public sealed partial class SceneEngineSync
{
    /// <inheritdoc/>
    public async Task<EnvironmentSyncResult> UpdateEnvironmentAsync(
        Scene scene,
        SceneEnvironmentData environment,
        SceneSyncRevision revision,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(environment);
        var scope = Scope(scene);
        if (cancellationToken.IsCancellationRequested)
        {
            var cancelled = Cancelled(SceneOperationKinds.EditEnvironment, scope);
            return EnvironmentResult(cancelled, cancelled, cancelled);
        }

        Task<EnvironmentSyncResult> application;
        DocumentLifetime lifetime;
        PendingEnvironmentSync request;
        lock (this.documentGate)
        {
            if (!this.TryGetDocument(scene, out lifetime!) || lifetime.Id != revision.DocumentLifetime
                || (lifetime.SnapshotBoundary is { } boundary && revision.IsAtOrBefore(boundary))
                || (lifetime.EnvironmentRevision is { } latest && revision.IsAtOrBefore(latest)))
            {
                var superseded = Cancelled(SceneOperationKinds.EditEnvironment, scope) with { Message = "The environment delivery was superseded or its document closed." };
                return EnvironmentResult(superseded, superseded, superseded);
            }

            request = new(Guid.NewGuid(), revision, environment);
            lifetime.EnvironmentRevision = revision;
            lifetime.PendingEnvironment = request;
            if (this.projecting?.Lifetime.Id == lifetime.Id)
            {
                var pending = RuntimeWorldUnavailable(SceneOperationKinds.EditEnvironment, scope) with { Message = "The environment will replay after the current scene snapshot." };
                this.OnPendingPropertySyncCountChanged(scene.Id, this.GetPendingPropertySyncCount(scene.Id));
                return ScopeEnvironmentResult(EnvironmentResult(pending, pending, pending), lifetime, revision);
            }

            application = this.ApplyEnvironmentAsync(scene, environment, cancellationToken);
        }

        var result = ScopeEnvironmentResult(await application.ConfigureAwait(false), lifetime, revision);
        this.CompleteEnvironmentDelivery(lifetime, request, result);
        return result;
    }

    private static EnvironmentSyncResult ScopeEnvironmentResult(EnvironmentSyncResult result, DocumentLifetime lifetime, SceneSyncRevision revision)
        => result with
        {
            PerField = result.PerField.ToDictionary(
                pair => pair.Key,
                pair => pair.Value with
                {
                    Scope = pair.Value.Scope with
                    {
                        DocumentId = lifetime.Metadata.DocumentId,
                        DocumentLifetime = lifetime.Id,
                        AuthoringRevision = revision.Revision,
                    },
                },
                StringComparer.Ordinal),
        };

    private void CompleteEnvironmentDelivery(DocumentLifetime lifetime, PendingEnvironmentSync request, EnvironmentSyncResult result)
    {
        lock (this.documentGate)
        {
            if (!this.IsDocumentCurrent(lifetime) || lifetime.PendingEnvironment?.RequestId != request.RequestId)
            {
                return;
            }

            lifetime.PendingEnvironment = result.Overall == SyncStatus.Accepted ? null : request with { Failure = result };
            this.OnPendingPropertySyncCountChanged(lifetime.Scene.Id, this.GetPendingPropertySyncCount(lifetime.Scene.Id));
        }
    }

    private void SupersedePendingSyncs(SceneProjection projection)
    {
        this.pendingPropertySyncs.Supersede(projection.Snapshot.Id, projection.Boundary);
        projection.Lifetime.SnapshotBoundary = projection.Boundary;
        foreach (var mutation in projection.Lifetime.PendingMutations.Values.Where(value => value.Revision.IsAtOrBefore(projection.Boundary)).ToArray())
        {
            _ = projection.Lifetime.PendingMutations.Remove(mutation.RequestId);
            _ = mutation.Completion?.TrySetResult(Accepted(mutation.OperationKind, mutation.Scope));
        }

        if (projection.Lifetime.PendingEnvironment is { } pending && pending.Revision.IsAtOrBefore(projection.Boundary))
        {
            projection.Lifetime.PendingEnvironment = null;
        }
    }

    private async Task ReplayPendingEnvironmentAsync(SceneProjection projection, PendingEnvironmentSync request, WorldDispatch world)
    {
        Task<EnvironmentSyncResult> application;
        lock (this.documentGate)
        {
            if (!this.IsDocumentCurrent(projection.Lifetime) || this.activeWorld?.Target != world.Target
                || projection.Lifetime.PendingEnvironment?.RequestId != request.RequestId)
            {
                return;
            }

            application = this.ApplyEnvironmentAsync(projection.Lifetime.Scene, request.Environment, world.CancellationToken, world);
        }

        var result = ScopeEnvironmentResult(await application.ConfigureAwait(false), projection.Lifetime, request.Revision);
        this.CompleteEnvironmentDelivery(projection.Lifetime, request, result);
        if (result.Overall != SyncStatus.Accepted && this.IsDocumentCurrent(projection.Lifetime))
        {
            this.PublishReplayFailures(request.RequestId, result.PerField.Values.Where(value => value.Status != SyncStatus.Accepted));
        }
    }

    private sealed record PendingEnvironmentSync(
        Guid RequestId,
        SceneSyncRevision Revision,
        SceneEnvironmentData Environment,
        EnvironmentSyncResult? Failure = null);
}
