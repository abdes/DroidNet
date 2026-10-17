// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Oxygen.Editor.World;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LivePreview.Scenes;

/// <summary>Delivers property values in edit order, keeping those the engine cannot take yet for replay.</summary>
internal sealed partial class ScenePreview
{
    private volatile SceneProjection? projecting;

    internal Task<SyncOutcome> UpdatePropertiesAsync(
        Scene scene,
        SceneNode node,
        IReadOnlyList<EnginePropertyValueEntry> entries,
        SceneSyncRevision revision,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(node);
        ArgumentNullException.ThrowIfNull(entries);
        var operationKind = GetPropertyOperationKind(entries);
        var scope = Scope(scene, node, componentType: GetPropertyComponentType(entries));
        if (cancellationToken.IsCancellationRequested)
        {
            return Task.FromResult(Cancelled(operationKind, scope));
        }

        if (entries.Count == 0)
        {
            return Task.FromResult(Accepted(operationKind, scope));
        }

        lock (this.documentGate)
        {
            if (!this.TryGetDocument(scene, out var lifetime) || lifetime.Id != revision.DocumentLifetime
                || !ReferenceEquals(FindNode(scene, node.Id), node))
            {
                return Task.FromResult(Cancelled(operationKind, scope) with { Message = "The source document lifetime is no longer open." });
            }

            _ = this.pendingPropertySyncs.Enqueue(scene.Id, revision, node.Id, entries);
            var pending = this.pendingPropertySyncs.Snapshot(scene.Id, lifetime.Id)
                .FirstOrDefault(request => request.Revision == revision && request.NodeId == node.Id);
            if (pending is null)
            {
                return Task.FromResult(Cancelled(operationKind, scope) with { Message = "A newer property value or scene snapshot superseded this delivery." });
            }

            var outcome = this.DeliverPendingProperty(scene, node, pending, operationKind, scope, cancellationToken);
            this.OnPendingFieldsChanged(scene.Id);
            return Task.FromResult(outcome);
        }
    }

    private static PendingWorldMutation? NextPendingMutation(DocumentLifetime lifetime, HashSet<Guid> attempted)
        => lifetime.PendingMutations.Values.Where(value => !attempted.Contains(value.RequestId))
            .OrderBy(value => value.Revision.Revision).ThenBy(value => value.Revision.Sequence).FirstOrDefault();

    private static PendingEnvironmentSync? NextPendingEnvironment(DocumentLifetime lifetime, HashSet<Guid> attempted)
        => lifetime.PendingEnvironment is { } pending && !attempted.Contains(pending.RequestId) ? pending : null;

    private SyncOutcome DeliverPendingProperty(
        Scene scene,
        SceneNode node,
        PendingPropertySyncEntry pending,
        string operationKind,
        AffectedScope scope,
        CancellationToken cancellationToken)
    {
        SyncOutcome outcome;
        if (this.projecting?.Lifetime.Id == pending.Revision.DocumentLifetime)
        {
            outcome = RuntimeWorldUnavailable(operationKind, scope) with { Message = "The current scene snapshot is still being projected." };
        }
        else if (!this.TryGetReadyWorld(this.engineService, scene, operationKind, scope, cancellationToken, out var world, out outcome))
        {
            outcome = this.ApplyPropertySync(scene, node, pending.Entries, scope, world!, operationKind, cancellationToken);
        }

        if (outcome.Status == SyncStatus.Accepted)
        {
            this.pendingPropertySyncs.Acknowledge(scene.Id, pending);
            return outcome;
        }

        this.pendingPropertySyncs.Fail(scene.Id, pending, outcome);
        var count = this.GetPendingPropertySyncCount(scene.Id);
        return outcome with
        {
            Message = string.Create(CultureInfo.InvariantCulture, $"{outcome.Message} {count} pending property edit(s) will retry after scene sync."),
        };
    }

    private async Task<bool> ReplayPendingSceneSyncsAsync(SceneProjection projection, WorldDispatch world)
    {
        var scene = projection.Lifetime.Scene;
        var attempted = new HashSet<Guid>();
        while (true)
        {
            PendingPropertySyncEntry? property;
            PendingEnvironmentSync? environment;
            PendingWorldMutation? selectedMutation = null;
            lock (this.documentGate)
            {
                if (!this.IsDocumentCurrent(projection.Lifetime) || this.activeWorld?.Target != world.Target)
                {
                    return false;
                }

                property = this.pendingPropertySyncs.Snapshot(scene.Id, projection.Lifetime.Id)
                    .FirstOrDefault(request => !attempted.Contains(request.RequestId));
                environment = NextPendingEnvironment(projection.Lifetime, attempted);

                var mutation = NextPendingMutation(projection.Lifetime, attempted);
                if (mutation is not null
                    && (property is null || mutation.Revision.IsAtOrBefore(property.Revision))
                    && (environment is null || mutation.Revision.IsAtOrBefore(environment.Revision)))
                {
                    _ = attempted.Add(mutation.RequestId);
                    selectedMutation = mutation;
                }
                else if (property is not null && (environment is null || property.Revision.IsAtOrBefore(environment.Revision)))
                {
                    _ = attempted.Add(property.RequestId);
                    if (this.pendingPropertySyncs.Current(scene.Id, property) is { } current)
                    {
                        this.ReplayPendingProperty(scene, current, world);
                    }

                    continue;
                }
            }

            if (selectedMutation is not null)
            {
                await this.ReplayWorldMutationAsync(projection, selectedMutation, world).ConfigureAwait(false);
                continue;
            }

            if (environment is null)
            {
                break;
            }

            _ = attempted.Add(environment.RequestId);
            await this.ReplayPendingEnvironmentAsync(projection, environment, world).ConfigureAwait(false);
        }

        var remaining = this.GetPendingPropertySyncCount(scene.Id);
        this.OnPendingFieldsChanged(scene.Id);
        return remaining == 0;
    }

    private void ReplayPendingProperty(Scene scene, PendingPropertySyncEntry pending, WorldDispatch world)
    {
        if (FindNode(scene, pending.NodeId) is not { } node)
        {
            this.pendingPropertySyncs.Acknowledge(scene.Id, pending);
            return;
        }

        var entries = pending.Entries.Where(entry => SceneWire.HasComponent(node, entry.Component)).ToArray();
        if (entries.Length == 0)
        {
            this.pendingPropertySyncs.Acknowledge(scene.Id, pending);
            return;
        }

        var operationKind = GetPropertyOperationKind(pending.Entries);
        var scope = Scope(scene, node, componentType: GetPropertyComponentType(pending.Entries));
        var outcome = this.ApplyPropertySync(scene, node, entries, scope, world, operationKind, world.CancellationToken);
        if (outcome.Status == SyncStatus.Accepted)
        {
            this.pendingPropertySyncs.Acknowledge(scene.Id, pending);
        }
        else
        {
            this.pendingPropertySyncs.Fail(scene.Id, pending, outcome);
            this.PublishReplayFailures(pending.RequestId, [outcome]);
        }
    }

    private void PublishReplayFailures(Guid requestId, IEnumerable<SyncOutcome> outcomes)
    {
        var failures = outcomes.DistinctBy(value => (value.Status, value.Code, value.Message)).ToArray();
        if (failures.Length == 0)
        {
            return;
        }

        var first = failures[0];
        this.operationResults?.Publish(new OperationResult
        {
            OperationId = requestId,
            OperationKind = first.OperationKind,
            Status = OperationStatus.Failed,
            Severity = DiagnosticSeverity.Error,
            Title = "Scene changes are still waiting for runtime sync",
            Message = first.Message ?? "Runtime replay did not accept the authored values.",
            AffectedScope = first.Scope,
            CompletedAt = DateTimeOffset.UtcNow,
            Diagnostics = [.. failures.Select(outcome => new DiagnosticRecord
            {
                OperationId = requestId,
                Domain = FailureDomain.LiveSync,
                Severity = DiagnosticSeverity.Error,
                Code = outcome.Code ?? LiveSyncDiagnosticCodes.NotRunning,
                Message = outcome.Message ?? "Runtime replay failed.",
                TechnicalMessage = outcome.Exception?.ToString(),
                ExceptionType = outcome.Exception?.GetType().FullName,
                AffectedEntity = outcome.Scope,
            })],
        });
    }
}
