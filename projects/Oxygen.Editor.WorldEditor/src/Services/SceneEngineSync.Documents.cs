// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Engine;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Services;

/// <summary>Owns each open scene's projection lifetime and the authoring revision that orders its projections.</summary>
public sealed partial class SceneEngineSync
{
    private readonly Lock documentGate = new();
    private readonly Dictionary<DocumentId, DocumentRegistration> registrations = [];
    private readonly Dictionary<SceneId, DocumentLifetime> openScenes = [];

    private bool Register(Scene scene, DocumentId documentId)
    {
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

            var registration = this.Registration(documentId);
            if (registration.Closed)
            {
                return false;
            }

            if (this.openScenes.TryGetValue(scene.Id, out var previous))
            {
                if (ReferenceEquals(previous.Scene, scene) && previous.DocumentId == documentId)
                {
                    return true;
                }

                this.RetireDocument(previous);
                if (previous.DocumentId != documentId)
                {
                    this.Registration(previous.DocumentId).Closed = true;
                }
            }

            var lifetime = new DocumentLifetime(scene, documentId);
            registration.Lifetime = lifetime;
            this.openScenes[scene.Id] = lifetime;
            this.pendingPropertySyncs.Register(scene.Id, lifetime.Id);
        }

        this.OnPendingPropertySyncCountChanged(scene.Id, pendingCount: 0);
        return true;
    }

    /// <summary>Gets the runtime activation that last accepted a document's scene.</summary>
    /// <param name="documentId">The document's identity.</param>
    /// <returns>The activation, or <see langword="null"/> when the document's scene was not synchronized.</returns>
    public RuntimeSceneTarget? GetSynchronizedTarget(DocumentId documentId)
    {
        lock (this.documentGate)
        {
            return this.registrations.TryGetValue(documentId, out var registration) && !registration.Closed
                ? registration.Lifetime?.LastSynchronizedTarget
                : null;
        }
    }

    /// <inheritdoc/>
    public Scene? GetDocumentScene(DocumentId documentId)
    {
        lock (this.documentGate)
        {
            return this.registrations.TryGetValue(documentId, out var registration)
                && !registration.Closed && registration.Lifetime is { } lifetime && this.IsDocumentCurrent(lifetime)
                ? lifetime.Scene
                : null;
        }
    }

    /// <inheritdoc/>
    public bool IsSceneSynchronized(Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);
        lock (this.documentGate)
        {
            return this.IsSceneProjectionCurrent(scene) && ReferenceEquals(this.requestedScene, scene);
        }
    }

    /// <inheritdoc/>
    public void CloseDocument(DocumentId documentId)
    {
        SceneId? sceneId = null;
        lock (this.documentGate)
        {
            // Retain a closed marker even if loading has not registered a scene yet.
            var registration = this.Registration(documentId);
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

    /// <summary>Projects one change into the engine; the projector calls it.</summary>
    internal SceneSyncRevision CaptureRevision(Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);
        lock (this.documentGate)
        {
            return this.TryGetDocument(scene, out var lifetime)
                ? new(lifetime.Id, lifetime.Revision, ++lifetime.Sequence)
                : default;
        }
    }

    /// <summary>Projects one change into the engine; the projector calls it.</summary>
    internal SceneSyncRevision AdvanceRevision(Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);
        lock (this.documentGate)
        {
            return this.TryGetDocument(scene, out var lifetime)
                ? new(lifetime.Id, ++lifetime.Revision, ++lifetime.Sequence)
                : default;
        }
    }

    private DocumentRegistration Registration(DocumentId documentId)
    {
        if (!this.registrations.TryGetValue(documentId, out var registration))
        {
            registration = new();
            this.registrations.Add(documentId, registration);
        }

        return registration;
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
        this.StopProjecting(lifetime);
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
            var boundary = new SceneSyncRevision(lifetime.Id, lifetime.Revision, lifetime.Sequence);
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
                this.SceneSynchronized?.Invoke(this, new(projection.Lifetime.Scene, projection.Lifetime.DocumentId));
                return true;
            }
        }

        return hostingContext?.Dispatcher is { HasThreadAccess: false } dispatcher
            ? dispatcher.DispatchAsync(Publish)
            : Task.FromResult(Publish());
    }

    private Task<bool> PublishExistingSceneReadyAsync(Scene scene)
    {
        bool Publish()
        {
            lock (this.documentGate)
            {
                if (!this.IsSceneProjectionCurrent(scene) || !ReferenceEquals(this.requestedScene, scene)
                    || !this.TryGetDocument(scene, out var lifetime)
                    || lifetime.LastSynchronizedTarget is null)
                {
                    return false;
                }

                this.SceneSynchronized?.Invoke(this, new(scene, lifetime.DocumentId));
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

    private sealed class DocumentLifetime(Scene scene, DocumentId documentId)
    {
        public Guid Id { get; } = Guid.NewGuid();

        public Scene Scene { get; } = scene;

        public DocumentId DocumentId { get; } = documentId;

        // Advances with every recorded edit, including undo and redo, so later projections supersede earlier ones.
        public long Revision { get; set; }

        public long Sequence { get; set; }

        public bool Retired { get; set; }

        public SceneSyncRevision? SnapshotBoundary { get; set; }

        public SceneSyncRevision? EnvironmentRevision { get; set; }

        public PendingEnvironmentSync? PendingEnvironment { get; set; }

        public RuntimeSceneTarget? LastSynchronizedTarget { get; set; }

        public Dictionary<Guid, PendingWorldMutation> PendingMutations { get; } = [];

        public SceneAuthoring? Authoring { get; set; }

        public EventHandler<SceneChangedEventArgs>? OnChanged { get; set; }

        // The projection of each change set starts after the previous one finished, in the order they were raised.
        public Task Projection { get; set; } = Task.CompletedTask;

        // Nodes and the environment whose referenced assets are not mounted yet.
        public HashSet<NodeId> AwaitingMount { get; } = [];

        public bool EnvironmentAwaitsMount { get; set; }

        // The material overrides each node had when the projector last saw it; an override that goes away is cleared.
        public Dictionary<NodeId, MaterialSlotTarget[]> MaterialOverrides { get; } = [];
    }

    private sealed record SceneProjection(DocumentLifetime Lifetime, Scene Snapshot, SceneSyncRevision Boundary)
    {
        public Dictionary<NodeId, NodeId?> NativeParents { get; } = Snapshot.AllNodes.ToDictionary(node => node.Id, node => node.Parent?.Id);
    }
}
