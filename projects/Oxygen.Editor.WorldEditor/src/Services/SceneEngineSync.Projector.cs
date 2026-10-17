// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Hosting.WinUI;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.Scenes.Engine;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Services;

/// <summary>Projects each open scene's changes into the engine, in the order the scene raised them.</summary>
/// <remarks>
/// <para>
/// The projector reads the scene when a change is projected, so a change always shows the scene's current state. Each
/// change set advances the document's revision when it is raised; a set the last full sync's snapshot already holds is
/// skipped, and one raised while a snapshot is being projected replays after it.
/// </para>
/// <para>
/// Property previews are throttled; a commit or a cancel ends the throttle and sends the final state. Within one change
/// set, a directional light that gives up an atmosphere slot is projected before the light that takes it, so the
/// engine never sees two owners. Geometry and an environment whose assets are not mounted yet wait until the asset feed
/// reports them mounted.
/// </para>
/// </remarks>
public sealed partial class SceneEngineSync
{
    private IReadOnlyList<AssetEntry> assetEntries = [];
    private IDisposable? assetFeedSubscription;

    /// <inheritdoc/>
    public bool RegisterDocument(SceneAuthoring authoring)
    {
        ArgumentNullException.ThrowIfNull(authoring);
        if (!this.Register(authoring.Scene, authoring.Document))
        {
            return false;
        }

        lock (this.documentGate)
        {
            if (!this.TryGetDocument(authoring.Scene, out var lifetime))
            {
                return false;
            }

            if (lifetime.Authoring is null)
            {
                lifetime.Authoring = authoring;
                RecordMaterialOverrides(lifetime, authoring.Scene.AllNodes);
                lifetime.OnChanged = (_, args) => this.Project(lifetime, args.Changes);
                authoring.Changed += lifetime.OnChanged;
            }
        }

        this.ObserveAssetFeed();
        return true;
    }

    // Moving a releasing light first keeps one owner per atmosphere slot.
    private static List<SceneChange> InProjectionOrder(Scene scene, IReadOnlyList<SceneChange> changes)
    {
        var slot = SceneProperties.DirectionalLight.AtmosphereSlotDescriptor.Id;
        bool Releases(SceneChange change)
            => change is SceneChange.PropertiesChanged { Node: { } node } properties
                && properties.Properties.Contains(slot)
                && FindNode(scene, node)?.Components.OfType<DirectionalLightComponent>().FirstOrDefault() is { AtmosphereSlot: AtmosphereLightSlot.None };

        return [.. changes.Where(Releases), .. changes.Where(change => !Releases(change))];
    }

    private static MaterialSlotTarget[] MaterialOverridesOf(SceneNode node)
        => [.. node.Components.OfType<GeometryComponent>().SelectMany(static geometry => geometry.OverrideSlots.OfType<MaterialsSlot>()).Select(static slot => slot.Target)];

    private static void RecordMaterialOverrides(DocumentLifetime lifetime, IEnumerable<SceneNode> nodes)
    {
        foreach (var node in nodes)
        {
            lifetime.MaterialOverrides[node.Id] = MaterialOverridesOf(node);
        }
    }

    private static bool IsInSnapshot(DocumentLifetime lifetime, SceneSyncRevision revision, SceneProjection? projecting)
        => (projecting is { } projection && ReferenceEquals(projection.Lifetime, lifetime) && revision.IsAtOrBefore(projection.Boundary))
            || (lifetime.SnapshotBoundary is { } boundary && revision.IsAtOrBefore(boundary));

    private static List<EnginePropertyValueEntry> EngineEntries(Scene scene, SceneNode node, IEnumerable<PropertyId> properties)
    {
        var entries = new List<EnginePropertyValueEntry>();
        foreach (var group in properties.GroupBy(static property => property.ComponentKind))
        {
            if (SceneProperties.RulesOf(group.Key) is not { } rules || rules.TargetOf(scene, node) is not { } target)
            {
                continue;
            }

            foreach (var property in group)
            {
                if (rules.Descriptors.TryGetValue(property, out var descriptor) && descriptor.Engine is { } mapping)
                {
                    var values = mapping.EncodeValue(descriptor.ReadBoxed(target));
                    entries.AddRange(mapping.Fields.Select((field, i) => new EnginePropertyValueEntry((EngineComponentId)mapping.Component, field, values[i])));
                }
            }
        }

        return entries;
    }

    private void Project(DocumentLifetime lifetime, IReadOnlyList<SceneChange> changes)
    {
        var revision = this.AdvanceRevision(lifetime.Scene);
        if (revision.DocumentLifetime == Guid.Empty)
        {
            return;
        }

        var ordered = InProjectionOrder(lifetime.Scene, changes);
        lifetime.Projection = this.ProjectAfterAsync(lifetime.Projection, lifetime, ordered, revision);
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "a failed projection is logged and must not stop the projection of later changes")]
    private async Task ProjectAfterAsync(Task previous, DocumentLifetime lifetime, List<SceneChange> changes, SceneSyncRevision revision)
    {
        await previous.ConfigureAwait(true);
        bool skip;
        lock (this.documentGate)
        {
            skip = !this.IsDocumentCurrent(lifetime) || IsInSnapshot(lifetime, revision, this.projecting);
        }

        if (skip)
        {
            return;
        }

        foreach (var change in changes)
        {
            try
            {
                await this.ProjectAsync(lifetime, change, revision).ConfigureAwait(true);
            }
            catch (Exception failure)
            {
                this.LogProjectionFailed(failure, change.GetType().Name);
            }
        }
    }

    private async Task ProjectAsync(DocumentLifetime lifetime, SceneChange change, SceneSyncRevision revision)
    {
        var scene = lifetime.Scene;
        switch (change)
        {
            case SceneChange.NodesAdded added:
                foreach (var node in added.Roots.Select(id => FindNode(scene, id)).OfType<SceneNode>())
                {
                    RecordMaterialOverrides(lifetime, [node, .. node.Descendants()]);
                    await this.CreateSubtreeAsync(node, node.Parent?.Id).ConfigureAwait(true);
                }

                break;
            case SceneChange.NodesRemoved removed:
                await this.RemoveNodeHierarchiesAsync(scene, removed.Roots).ConfigureAwait(true);
                break;
            case SceneChange.NodesPlaced placed:
                await this.ProjectPlacementAsync(scene, placed.Nodes).ConfigureAwait(true);
                break;
            case SceneChange.NodeRenamed renamed when FindNode(scene, renamed.Node) is { } node:
                this.Report(await this.RenameNodeAsync(scene, node.Id, node.Name).ConfigureAwait(true));
                break;
            case SceneChange.ComponentAdded added when FindNode(scene, added.Node) is { } node:
                await this.AttachAsync(lifetime, node, node.Components.FirstOrDefault(component => component.Id == added.Component)).ConfigureAwait(true);
                break;
            case SceneChange.ComponentRemoved removed:
                await this.DetachAsync(scene, removed.Node, removed.Kind).ConfigureAwait(true);
                break;
            case SceneChange.PropertiesChanged properties:
                await this.ProjectPropertiesAsync(lifetime, properties, revision).ConfigureAwait(true);
                break;
            case SceneChange.GeometryChanged geometry when FindNode(scene, geometry.Node) is { } node:
                RecordMaterialOverrides(lifetime, [node]);
                await this.AttachGeometryWhenMountedAsync(lifetime, node).ConfigureAwait(true);
                break;
            case SceneChange.MaterialSlotsChanged slots when FindNode(scene, slots.Node) is { } node:
                await this.ProjectMaterialSlotsAsync(lifetime, node).ConfigureAwait(true);
                break;
            default:
                // Layout, editor visibility, play, references and scripts have no engine projection.
                break;
        }
    }

    // The engine keeps a slot's material until told otherwise: an override that went away is cleared, and the overrides
    // the node has are sent once their materials are mounted. The geometry stays attached.
    private async Task ProjectMaterialSlotsAsync(DocumentLifetime lifetime, SceneNode node)
    {
        var current = MaterialOverridesOf(node);
        MaterialSlotTarget[] cleared = [.. lifetime.MaterialOverrides.GetValueOrDefault(node.Id, [])
            .Where(previous => !current.Any(target => target.GeometryUri == previous.GeometryUri && target.SlotId == previous.SlotId))];
        lifetime.MaterialOverrides[node.Id] = current;
        var mounted = this.IsMounted(SceneAssetReferences.Runtime(node));
        if (!mounted)
        {
            _ = lifetime.AwaitingMount.Add(node.Id);
        }

        if (mounted || cleared.Length > 0)
        {
            this.Report(await this.UpdateMaterialSlotsAsync(lifetime.Scene, node, cleared, overrides: mounted).ConfigureAwait(true));
        }
    }

    private async Task CreateSubtreeAsync(SceneNode node, NodeId? parent)
    {
        await this.CreateNodeAsync(node, parent).ConfigureAwait(true);
        foreach (var child in node.Children.ToArray())
        {
            await this.CreateSubtreeAsync(child, node.Id).ConfigureAwait(true);
        }
    }

    // The scene already holds each node's compensated local transform: push it, then reparent without compensation.
    private async Task ProjectPlacementAsync(Scene scene, IReadOnlyList<NodeId> ids)
    {
        var nodes = ids.Select(id => FindNode(scene, id)).OfType<SceneNode>().ToList();
        foreach (var node in nodes)
        {
            this.Report(await this.UpdateNodeTransformAsync(scene, node).ConfigureAwait(true));
        }

        foreach (var group in nodes.GroupBy(static node => node.Parent?.Id))
        {
            await this.ReparentHierarchiesAsync(scene, [.. group.Select(static node => node.Id)], group.Key, preserveWorldTransform: false).ConfigureAwait(true);
        }
    }

    private async Task AttachAsync(DocumentLifetime lifetime, SceneNode node, GameComponent? component)
    {
        var scene = lifetime.Scene;
        switch (component)
        {
            case GeometryComponent:
                await this.AttachGeometryWhenMountedAsync(lifetime, node).ConfigureAwait(true);
                break;
            case LightComponent:
                this.Report(await this.AttachLightAsync(scene, node).ConfigureAwait(true));
                break;
            case CameraComponent:
                this.Report(await this.AttachCameraAsync(scene, node).ConfigureAwait(true));
                break;
            case LocalFogVolumeComponent:
                this.Report(await this.AttachLocalFogVolumeAsync(scene, node).ConfigureAwait(true));
                break;
            default:
                break;
        }
    }

    private async Task DetachAsync(Scene scene, NodeId node, string kind)
    {
        var outcome = kind switch
        {
            SceneComponents.Geometry => await this.DetachGeometryAsync(scene, node).ConfigureAwait(true),
            SceneComponents.DirectionalLight or SceneComponents.PointLight or SceneComponents.SpotLight => await this.DetachLightAsync(scene, node).ConfigureAwait(true),
            SceneComponents.PerspectiveCamera or SceneComponents.OrthographicCamera => await this.DetachCameraAsync(scene, node).ConfigureAwait(true),
            SceneComponents.LocalFogVolume => await this.DetachLocalFogVolumeAsync(scene, node).ConfigureAwait(true),
            _ => null,
        };
        this.Report(outcome);
    }

    private async Task ProjectPropertiesAsync(DocumentLifetime lifetime, SceneChange.PropertiesChanged change, SceneSyncRevision revision)
    {
        var scene = lifetime.Scene;
        Func<CancellationToken, Task<SyncOutcome>> sync;
        Guid target;
        if (change.Node is not { } id)
        {
            if (change.Phase == EditPhase.Commit && !this.IsMounted(SceneAssetReferences.Environment(scene.Environment)))
            {
                lifetime.EnvironmentAwaitsMount = true;
                return;
            }

            target = scene.Id.Value;
            sync = async token => this.EnvironmentOutcome(scene, await this.UpdateEnvironmentAsync(scene, scene.Environment, revision, token).ConfigureAwait(true));
        }
        else if (FindNode(scene, id) is { } node)
        {
            target = id.Value;
            var transform = change.Properties.Any(static property => property.ComponentKind == SceneProperties.TransformKind);
            var entries = EngineEntries(scene, node, change.Properties);
            if (!transform && entries.Count == 0)
            {
                return;
            }

            sync = async token =>
            {
                var moved = transform ? await this.UpdateNodeTransformAsync(scene, node, token).ConfigureAwait(true) : null;
                return entries.Count == 0 || moved is { Status: not SyncStatus.Accepted }
                    ? moved!
                    : await this.UpdatePropertiesAsync(scene, node, entries, revision, token).ConfigureAwait(true);
            };
        }
        else
        {
            return;
        }

        var outcome = change.Phase switch
        {
            EditPhase.Preview => await this.TryPreviewSyncAsync(scene.Id, target, DateTimeOffset.UtcNow, sync).ConfigureAwait(true),
            EditPhase.Cancel => await this.CancelPreviewSyncAsync(scene.Id, target, sync).ConfigureAwait(true),
            _ => await this.CompleteTerminalSyncAsync(scene.Id, target, sync).ConfigureAwait(true),
        };
        this.Report(outcome);
    }

    private async Task AttachGeometryWhenMountedAsync(DocumentLifetime lifetime, SceneNode node)
    {
        if (!this.IsMounted(SceneAssetReferences.Runtime(node)))
        {
            _ = lifetime.AwaitingMount.Add(node.Id);
            return;
        }

        _ = lifetime.AwaitingMount.Remove(node.Id);
        this.Report(await this.AttachGeometryAsync(lifetime.Scene, node).ConfigureAwait(true));
    }

    // An asset the feed does not know, such as a generated shape, is taken as ready.
    private bool IsMounted(IEnumerable<Uri> references)
        => references.All(uri => this.assetEntries.Find(uri) is not { } entry
            || entry.RuntimeAvailability is AssetRuntimeAvailability.Mounted or AssetRuntimeAvailability.NotApplicable);

    private SyncOutcome EnvironmentOutcome(Scene scene, EnvironmentSyncResult result)
        => result.PerField.Values.FirstOrDefault(static outcome => outcome.Status is SyncStatus.Rejected or SyncStatus.Failed)
            ?? new SyncOutcome(result.Overall, SceneOperationKinds.EditEnvironment, Scope(scene));

    private void ObserveAssetFeed()
    {
        lock (this.documentGate)
        {
            if (this.disposed || this.assetFeedSubscription is not null || assetFeed is null)
            {
                return;
            }

            this.assetFeedSubscription = assetFeed.Items.Subscribe(entries =>
            {
                if (hostingContext?.Dispatcher is { HasThreadAccess: false } dispatcher)
                {
                    _ = dispatcher.DispatchAsync(() => this.OnAssetsChanged(entries));
                }
                else
                {
                    this.OnAssetsChanged(entries);
                }
            });
        }
    }

    private void OnAssetsChanged(IReadOnlyList<AssetEntry> entries)
    {
        this.assetEntries = entries;
        List<DocumentLifetime> waiting;
        lock (this.documentGate)
        {
            waiting = [.. this.openScenes.Values.Where(static lifetime => lifetime.AwaitingMount.Count != 0 || lifetime.EnvironmentAwaitsMount)];
        }

        foreach (var lifetime in waiting)
        {
            var nodes = lifetime.AwaitingMount.Select(id => FindNode(lifetime.Scene, id)).OfType<SceneNode>()
                .Where(node => this.IsMounted(SceneAssetReferences.Runtime(node))).Select(static node => node.Id).ToList();
            List<SceneChange> ready = [.. nodes.Select(static id => new SceneChange.GeometryChanged(id))];
            lifetime.AwaitingMount.IntersectWith(lifetime.Scene.AllNodes.Select(static node => node.Id).Except(nodes));
            if (lifetime.EnvironmentAwaitsMount && this.IsMounted(SceneAssetReferences.Environment(lifetime.Scene.Environment)))
            {
                lifetime.EnvironmentAwaitsMount = false;
                ready.Add(new SceneChange.PropertiesChanged(Node: null, [], EditPhase.Commit));
            }

            if (ready.Count != 0)
            {
                this.Project(lifetime, ready);
            }
        }
    }

    private void StopProjecting(DocumentLifetime lifetime)
    {
        if (lifetime.Authoring is { } authoring && lifetime.OnChanged is { } handler)
        {
            authoring.Changed -= handler;
        }

        lifetime.OnChanged = null;
        lifetime.AwaitingMount.Clear();
        lifetime.EnvironmentAwaitsMount = false;
    }

    // Engine outcomes are reported here: the edit succeeded when the scene changed.
    private void Report(SyncOutcome? outcome)
    {
        if (outcome is not { Status: SyncStatus.Rejected or SyncStatus.Failed })
        {
            return;
        }

        var operation = Guid.NewGuid();
        operationResults?.Publish(new OperationResult
        {
            OperationId = operation,
            OperationKind = outcome.OperationKind,
            Status = OperationStatus.Failed,
            Severity = DiagnosticSeverity.Warning,
            Title = "The engine did not take a scene change",
            Message = outcome.Message ?? "The engine rejected the change; the scene keeps it.",
            AffectedScope = outcome.Scope,
            CompletedAt = DateTimeOffset.UtcNow,
            Diagnostics =
            [
                new DiagnosticRecord
                {
                    OperationId = operation,
                    Domain = FailureDomain.LiveSync,
                    Severity = DiagnosticSeverity.Warning,
                    Code = outcome.Code ?? LiveSyncDiagnosticCodes.MutationFailed,
                    Message = outcome.Message ?? "The engine rejected the change.",
                    TechnicalMessage = outcome.Exception?.ToString(),
                    ExceptionType = outcome.Exception?.GetType().FullName,
                    AffectedEntity = outcome.Scope,
                },
            ],
        });
    }
}
