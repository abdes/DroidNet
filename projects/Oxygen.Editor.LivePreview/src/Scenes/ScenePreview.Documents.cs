// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.LivePreview.Scenes;

/// <summary>Follows the open scene documents and owns each one's projection lifetime and readiness.</summary>
internal sealed partial class ScenePreview
{
    private readonly Lock documentGate = new();
    private readonly OpenDocuments documents;
    private readonly Dictionary<SceneId, DocumentLifetime> openScenes = [];

    // In the order they opened: the engine shows the last one.
    private readonly List<FollowedDocument> followed = [];
    private readonly HashSet<DocumentId> ready = [];
    private OwnerThread owner = OwnerThread.Capture();
    private DocumentId? shown;

    /// <inheritdoc/>
    public event EventHandler<ScenePreviewEventArgs>? ReadyChanged;

    /// <inheritdoc/>
    public event EventHandler<ScenePreviewEventArgs>? PendingFieldsChanged;

    /// <inheritdoc/>
    public bool IsReady(DocumentId document)
    {
        lock (this.documentGate)
        {
            return this.ready.Contains(document);
        }
    }

    /// <inheritdoc/>
    public int PendingFieldCount(DocumentId document)
    {
        lock (this.documentGate)
        {
            return this.LifetimeOf(document) is { } lifetime ? this.GetPendingPropertySyncCount(lifetime.Scene.Id) : 0;
        }
    }

    /// <inheritdoc/>
    public Task<SkyLightObservation?> ObserveSkyLightAsync(DocumentId document, CancellationToken cancellationToken = default)
    {
        Scene? scene;
        lock (this.documentGate)
        {
            scene = this.LifetimeOf(document)?.Scene;
        }

        return scene is null ? Task.FromResult<SkyLightObservation?>(null) : this.ObserveSkyLightAsync(scene, cancellationToken);
    }

    /// <summary>Gets the engine activation that shows a document's scene, for observing what the engine shows.</summary>
    /// <param name="document">The scene document.</param>
    /// <returns>The activation, or <see langword="null"/> when the document is not ready.</returns>
    internal RuntimeSceneTarget? ShownTargetOf(DocumentId document)
    {
        lock (this.documentGate)
        {
            return this.ready.Contains(document) ? this.LifetimeOf(document)?.LastSynchronizedTarget : null;
        }
    }

    /// <summary>Registers the content of a scene document, replacing the content the document had before.</summary>
    /// <remarks>
    /// Call it on the document's owner thread. From then on the engine follows the scene's changes: every change raised
    /// after the call is projected after the next full sync, and none raised before it, since the sync's snapshot holds
    /// those.
    /// </remarks>
    /// <param name="content">The document's scene content.</param>
    /// <param name="documentOwner">The document's owner thread.</param>
    /// <returns><see langword="false"/> when the preview is disposed.</returns>
    internal bool RegisterDocument(SceneContent content, OwnerThread documentOwner)
    {
        ArgumentNullException.ThrowIfNull(content);
        var authoring = content.Authoring;
        if (!this.Register(authoring.Scene, authoring.Document, documentOwner))
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
                RecordOverrideParts(lifetime, authoring.Scene.AllNodes);
                RecordInstances(lifetime, authoring.Scene.AllNodes);
                lifetime.OnChanged = (_, args) => this.Project(lifetime, args.Changes);
                authoring.Changed += lifetime.OnChanged;
            }
        }

        this.ObserveAssetFeed();
        return true;
    }

    /// <summary>Stamps a projection that is not a new edit, such as a replay, with the document's current revision.</summary>
    /// <param name="scene">The scene of a followed document.</param>
    /// <returns>The stamp, or the default stamp when the scene's document is not followed.</returns>
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

    /// <summary>Stamps the projection of a new edit with the next revision, which supersedes the projections of earlier edits.</summary>
    /// <param name="scene">The scene of a followed document.</param>
    /// <returns>The stamp, or the default stamp when the scene's document is not followed.</returns>
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

    private void FollowDocuments()
    {
        this.engineService.StateChanged += this.OnRuntimeStateChanged;
        this.engineService.ContentStatusChanged += this.OnContentStatusChanged;
        this.documents.Opened += this.OnDocumentOpened;
        this.documents.Closed += this.OnDocumentClosed;
        foreach (var document in this.documents.Documents.ToArray())
        {
            this.OnDocumentOpened(this.documents, new DocumentEventArgs(document));
        }
    }

    private void StopFollowingDocuments()
    {
        this.engineService.StateChanged -= this.OnRuntimeStateChanged;
        this.engineService.ContentStatusChanged -= this.OnContentStatusChanged;
        this.documents.Opened -= this.OnDocumentOpened;
        this.documents.Closed -= this.OnDocumentClosed;
        foreach (var entry in this.followed)
        {
            entry.Document.ContentReplaced -= entry.OnContentReplaced;
        }

        this.followed.Clear();
    }

    private void OnDocumentOpened(object? sender, DocumentEventArgs args)
    {
        if (this.disposed || args.Document.Content is not SceneContent || this.followed.Exists(entry => ReferenceEquals(entry.Document, args.Document)))
        {
            return;
        }

        var document = args.Document;
        this.LogFollowingDocument(document);
        var entry = new FollowedDocument(document, OwnerThread.Capture());
        entry.OnContentReplaced = (_, _) => this.OnContentReplaced(entry);
        document.ContentReplaced += entry.OnContentReplaced;
        this.followed.Add(entry);
        this.Show(entry);
    }

    private void OnContentReplaced(FollowedDocument entry)
    {
        if (this.disposed || entry.Document.Content is not SceneContent content)
        {
            return;
        }

        bool isShown;
        lock (this.documentGate)
        {
            isShown = this.shown == entry.Document.Id;
        }

        if (isShown)
        {
            this.Show(entry);
        }
        else
        {
            _ = this.RegisterDocument(content, entry.Owner);
        }
    }

    private void OnDocumentClosed(object? sender, DocumentEventArgs args)
    {
        var index = this.followed.FindIndex(entry => ReferenceEquals(entry.Document, args.Document));
        if (index < 0)
        {
            return;
        }

        var closed = this.followed[index];
        this.followed.RemoveAt(index);
        closed.Document.ContentReplaced -= closed.OnContentReplaced;
        bool wasShown;
        lock (this.documentGate)
        {
            wasShown = this.shown == closed.Document.Id;
            if (wasShown)
            {
                this.shown = null;
            }
        }

        this.CloseDocument(closed);
        if (wasShown && this.followed.Count > 0)
        {
            this.Show(this.followed[^1]);
        }
    }

    // The engine shows one scene: showing a document retires the projection of the one shown before.
    private void Show(FollowedDocument entry)
    {
        var content = SceneContent.Of(entry.Document);
        if (!this.RegisterDocument(content, entry.Owner))
        {
            return;
        }

        this.LogShowingDocument(content.Scene, entry.Document.Id);
        lock (this.documentGate)
        {
            this.shown = entry.Document.Id;
            this.owner = entry.Owner;
            this.SelectRequestedScene(content.Scene);
        }

        if (this.engineService.State == EngineServiceState.Running)
        {
            _ = this.SyncSceneCoreAsync(content.Scene, skipIfCurrent: true, CancellationToken.None);
        }
        else
        {
            this.LogEngineNotRunningDeferringSceneSync(content.Scene);
        }
    }

    private bool Register(Scene scene, DocumentId documentId, OwnerThread documentOwner)
    {
        lock (this.documentGate)
        {
            if (this.disposed)
            {
                return false;
            }

            if (this.openScenes.TryGetValue(scene.Id, out var previous))
            {
                if (ReferenceEquals(previous.Scene, scene) && previous.DocumentId == documentId)
                {
                    return true;
                }

                this.RetireDocument(previous, ReferenceEquals(previous.Scene, scene) ? "another document opened the scene" : "its content was replaced");
            }

            var lifetime = new DocumentLifetime(scene, documentId, documentOwner);
            this.openScenes[scene.Id] = lifetime;
            this.pendingPropertySyncs.Register(scene.Id, lifetime.Id);
        }

        this.OnPendingFieldsChanged(scene.Id);
        return true;
    }

    private void CloseDocument(FollowedDocument closed)
    {
        lock (this.documentGate)
        {
            if (this.LifetimeOf(closed.Document.Id) is { } lifetime)
            {
                this.RetireDocument(lifetime, "its document closed");
            }
        }

        closed.Owner.Post(() => this.PendingFieldsChanged?.Invoke(this, new ScenePreviewEventArgs(closed.Document.Id)));
    }

    private DocumentLifetime? LifetimeOf(DocumentId document)
        => this.openScenes.Values.FirstOrDefault(lifetime => lifetime.DocumentId == document);

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

    private OwnerThread OwnerOf(Scene scene)
    {
        lock (this.documentGate)
        {
            return this.TryGetDocument(scene, out var lifetime) ? lifetime.Owner : this.owner;
        }
    }

    private void OnPendingFieldsChanged(SceneId sceneId)
    {
        DocumentLifetime? lifetime;
        lock (this.documentGate)
        {
            lifetime = this.openScenes.GetValueOrDefault(sceneId);
        }

        lifetime?.Owner.Queue(() => this.PendingFieldsChanged?.Invoke(this, new ScenePreviewEventArgs(lifetime.DocumentId)));
    }

    // Called under the document gate.
    private void SetReady(DocumentLifetime lifetime, bool isReady)
    {
        var changed = isReady ? this.ready.Add(lifetime.DocumentId) : this.ready.Remove(lifetime.DocumentId);
        if (changed)
        {
            lifetime.Owner.Queue(() => this.ReadyChanged?.Invoke(this, new ScenePreviewEventArgs(lifetime.DocumentId)));
        }

        // A ready document's content is mounted, possibly again after a cook: structures read before may differ.
        if (changed && isReady)
        {
            lifetime.Owner.Queue(() => this.StructuresChanged?.Invoke(this, EventArgs.Empty));
        }
    }

    private void RetireDocument(DocumentLifetime lifetime, string reason)
    {
        this.LogRetiredDocument(lifetime.Scene, lifetime.DocumentId, reason);
        lifetime.Retired = true;

        // Retiring runs under the document gate; the cancellation's callbacks run on the thread pool, outside it.
        _ = lifetime.Retirement.CancelAsync();
        this.SetReady(lifetime, isReady: false);
        StopProjecting(lifetime);
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
        => this.OwnerOf(scene).InvokeAsync(() => this.CaptureSceneProjectionOnOwnerThread(scene));

    private SceneProjection? CaptureSceneProjectionOnOwnerThread(Scene scene)
    {
        lock (this.documentGate)
        {
            if (!this.TryGetDocument(scene, out var lifetime))
            {
                return null;
            }

            var snapshot = Scene.CreateAndHydrate(scene.Dehydrate());
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

                if (!publishReady || this.GetPendingPropertySyncCount(projection.Snapshot.Id) > 0)
                {
                    return false;
                }

                this.projecting = null;
                projection.Lifetime.LastSynchronizedTarget = target;
                this.SetReady(projection.Lifetime, isReady: true);
                return true;
            }
        }

        return projection.Lifetime.Owner.InvokeAsync(Publish);
    }

    private bool PublishExistingSceneReady(Scene scene)
    {
        lock (this.documentGate)
        {
            if (!this.IsSceneProjectionCurrent(scene) || !ReferenceEquals(this.requestedScene, scene)
                || !this.TryGetDocument(scene, out var lifetime)
                || lifetime.LastSynchronizedTarget is null)
            {
                return false;
            }

            this.SetReady(lifetime, isReady: true);
            return true;
        }
    }

    private sealed class FollowedDocument(Document document, OwnerThread owner)
    {
        public Document Document { get; } = document;

        public OwnerThread Owner { get; } = owner;

        public EventHandler<DocumentContentReplacedEventArgs>? OnContentReplaced { get; set; }
    }

    private sealed class DocumentLifetime(Scene scene, DocumentId documentId, OwnerThread owner)
    {
        public Guid Id { get; } = Guid.NewGuid();

        public Scene Scene { get; } = scene;

        public DocumentId DocumentId { get; } = documentId;

        // The thread that owns the document: its scene is read and its events are raised there.
        public OwnerThread Owner { get; } = owner;

        // Advances with every recorded edit, including undo and redo, so later projections supersede earlier ones.
        public long Revision { get; set; }

        public long Sequence { get; set; }

        public bool Retired { get; set; }

        // Cancelled when the lifetime retires, which stops the projection of its remaining changes. It is never disposed:
        // a projection still running may read its token, and a source without a timer holds no resource.
        public CancellationTokenSource Retirement { get; } = new();

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

        // The override parts each node had when the projector last saw it; a part that goes away is cleared.
        public Dictionary<NodeId, OverridePart[]> OverrideParts { get; } = [];

        // The instance each instance root had when the projector last sent it; only what differs is sent.
        public Dictionary<NodeId, ProjectedInstance> Instances { get; } = [];
    }

    private sealed record SceneProjection(DocumentLifetime Lifetime, Scene Snapshot, SceneSyncRevision Boundary)
    {
        public Dictionary<NodeId, NodeId?> NativeParents { get; } = Snapshot.AllNodes.ToDictionary(node => node.Id, node => node.Hierarchy.Parent?.Id);
    }
}
