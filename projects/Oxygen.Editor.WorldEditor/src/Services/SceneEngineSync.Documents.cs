// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.CompilerServices;
using DroidNet.Hosting.WinUI;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Documents;

namespace Oxygen.Editor.World.Services;

/// <summary>Owns projection lifetimes while document metadata owns authoring revisions.</summary>
public sealed partial class SceneEngineSync
{
    private readonly Lock documentGate = new();
    private readonly ConditionalWeakTable<SceneDocumentMetadata, DocumentRegistration> registrations = [];
    private readonly Dictionary<Guid, DocumentLifetime> openScenes = [];

    /// <inheritdoc/>
    public bool RegisterDocument(Scene scene, SceneDocumentMetadata metadata)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(metadata);
        lock (this.documentGate)
        {
            if (this.disposed)
            {
                return false;
            }

            if (!this.observingEngine)
            {
                this.engineService.StateChanged += this.OnRuntimeStateChanged;
                this.observingEngine = true;
            }

            var registration = this.registrations.GetValue(metadata, _ => new());
            if (registration.Closed)
            {
                return false;
            }

            if (this.openScenes.TryGetValue(scene.Id, out var previous))
            {
                if (ReferenceEquals(previous.Scene, scene) && ReferenceEquals(previous.Metadata, metadata))
                {
                    return true;
                }

                this.RetireDocument(previous);
                if (!ReferenceEquals(previous.Metadata, metadata))
                {
                    this.registrations.GetValue(previous.Metadata, _ => new()).Closed = true;
                }
            }

            var lifetime = new DocumentLifetime(scene, metadata);
            registration.Lifetime = lifetime;
            this.openScenes[scene.Id] = lifetime;
            this.pendingPropertySyncs.Register(scene.Id, lifetime.Id);
        }

        this.OnPendingPropertySyncCountChanged(scene.Id, pendingCount: 0);
        return true;
    }

    /// <inheritdoc/>
    public Scene? GetDocumentScene(SceneDocumentMetadata metadata)
    {
        ArgumentNullException.ThrowIfNull(metadata);
        lock (this.documentGate)
        {
            return this.registrations.TryGetValue(metadata, out var registration)
                && !registration.Closed && registration.Lifetime is { } lifetime && this.IsDocumentCurrent(lifetime)
                ? lifetime.Scene
                : null;
        }
    }

    /// <inheritdoc/>
    public void CloseDocument(SceneDocumentMetadata metadata)
    {
        ArgumentNullException.ThrowIfNull(metadata);
        Guid? sceneId = null;
        lock (this.documentGate)
        {
            // Retain a weak closed marker even if loading has not registered a scene yet.
            var registration = this.registrations.GetValue(metadata, _ => new());
            registration.Closed = true;
            if (registration.Lifetime is { } lifetime)
            {
                sceneId = lifetime.Scene.Id;
                this.RetireDocument(lifetime);
            }
        }

        if (sceneId is { } id)
        {
            this.OnPendingPropertySyncCountChanged(id, this.pendingPropertySyncs.Count(id));
        }
    }

    /// <inheritdoc/>
    public SceneSyncRevision CaptureRevision(Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);
        lock (this.documentGate)
        {
            return this.TryGetDocument(scene, out var lifetime)
                ? new(lifetime.Id, lifetime.Metadata.ChangeVersion, ++lifetime.Sequence)
                : default;
        }
    }

    /// <inheritdoc/>
    public SceneSyncRevision CaptureRevision(Scene scene, SceneDocumentMetadata metadata)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(metadata);
        lock (this.documentGate)
        {
            return this.TryGetDocument(scene, out var lifetime) && ReferenceEquals(lifetime.Metadata, metadata)
                ? new(lifetime.Id, lifetime.Metadata.ChangeVersion, ++lifetime.Sequence)
                : default;
        }
    }

    private bool TryGetDocument(Scene scene, out DocumentLifetime lifetime)
        => this.openScenes.TryGetValue(scene.Id, out lifetime!)
            && !lifetime.Retired && ReferenceEquals(lifetime.Scene, scene);

    private bool IsDocumentCurrent(DocumentLifetime lifetime)
    {
        lock (this.documentGate)
        {
            return this.TryGetDocument(lifetime.Scene, out var current) && ReferenceEquals(current, lifetime);
        }
    }

    private void RetireDocument(DocumentLifetime lifetime)
    {
        lifetime.Retired = true;
        this.coalescer.ResetScene(lifetime.Scene.Id);
        foreach (var pending in lifetime.PendingMutations.Values)
        {
            _ = pending.Completion?.TrySetResult(Cancelled(pending.OperationKind, pending.Scope));
        }

        lifetime.PendingMutations.Clear();
        lifetime.PendingEnvironment = null;
        this.pendingPropertySyncs.Close(lifetime.Scene.Id, lifetime.Id);
        if (this.openScenes.TryGetValue(lifetime.Scene.Id, out var current) && ReferenceEquals(current, lifetime))
        {
            if (ReferenceEquals(this.requestedScene, lifetime.Scene))
            {
                this.requestedScene = null;
            }

            _ = this.openScenes.Remove(lifetime.Scene.Id);
        }

        if (this.activeWorld is { } world && world.Target.DocumentLifetime == lifetime.Id)
        {
            world.Commands.InvalidateScene(world.Target);
            this.activeWorld = null;
            this.activeScene = null;
        }
    }

    private Task<SceneProjection?> CaptureSceneProjectionAsync(Scene scene)
        => hostingContext?.Dispatcher is { HasThreadAccess: false } dispatcher
            ? dispatcher.DispatchAsync(() => this.CaptureSceneProjectionOnUiThread(scene))
            : Task.FromResult(this.CaptureSceneProjectionOnUiThread(scene));

    private SceneProjection? CaptureSceneProjectionOnUiThread(Scene scene)
    {
        lock (this.documentGate)
        {
            if (!this.TryGetDocument(scene, out var lifetime))
            {
                return null;
            }

            var snapshot = Scene.CreateAndHydrate(scene.Project, scene.Dehydrate());
            var boundary = new SceneSyncRevision(lifetime.Id, lifetime.Metadata.ChangeVersion, lifetime.Sequence);
            return new(lifetime, snapshot, boundary);
        }
    }

    private Task<bool> PublishProjectedNodesAsync(SceneProjection projection, RuntimeSceneTarget target, bool publishReady)
    {
        bool Publish()
        {
            lock (this.documentGate)
            {
                if (!this.IsDocumentCurrent(projection.Lifetime) || this.activeWorld?.Target != target)
                {
                    return false;
                }

                var projected = projection.NativeParents.Keys.ToHashSet();
                foreach (var node in projection.Lifetime.Scene.AllNodes)
                {
                    node.IsActive = projected.Contains(node.Id);
                }

                if (!publishReady || this.GetPendingPropertySyncCount(projection.Snapshot.Id) > 0)
                {
                    return false;
                }

                this.projecting = null;
                projection.Lifetime.LastSynchronizedTarget = target;
                this.SceneSynchronized?.Invoke(this, new(projection.Lifetime.Scene, projection.Lifetime.Metadata, target));
                return true;
            }
        }

        return hostingContext?.Dispatcher is { HasThreadAccess: false } dispatcher
            ? dispatcher.DispatchAsync(Publish)
            : Task.FromResult(Publish());
    }

    private sealed class DocumentRegistration
    {
        public bool Closed { get; set; }

        public DocumentLifetime? Lifetime { get; set; }
    }

    private sealed class DocumentLifetime(Scene scene, SceneDocumentMetadata metadata)
    {
        public Guid Id { get; } = Guid.NewGuid();

        public Scene Scene { get; } = scene;

        public SceneDocumentMetadata Metadata { get; } = metadata;

        public long Sequence { get; set; }

        public bool Retired { get; set; }

        public SceneSyncRevision? SnapshotBoundary { get; set; }

        public SceneSyncRevision? EnvironmentRevision { get; set; }

        public PendingEnvironmentSync? PendingEnvironment { get; set; }

        public RuntimeSceneTarget? LastSynchronizedTarget { get; set; }

        public Dictionary<Guid, PendingWorldMutation> PendingMutations { get; } = [];
    }

    private sealed record SceneProjection(DocumentLifetime Lifetime, Scene Snapshot, SceneSyncRevision Boundary)
    {
        public Dictionary<Guid, Guid?> NativeParents { get; } = Snapshot.AllNodes.ToDictionary(node => node.Id, node => node.Parent?.Id);
    }
}
