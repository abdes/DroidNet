// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World;

namespace Oxygen.Editor.LivePreview.Scenes;

/// <summary>Shows the requested scene again when the engine runs again, and disposes the preview.</summary>
internal sealed partial class ScenePreview
{
    private readonly CancellationTokenSource disposal = new();
    private Scene? requestedScene;
    private volatile bool disposed;
    private int syncOperations;
    private int syncGateDisposed;

    /// <inheritdoc/>
    public void Dispose()
    {
        lock (this.documentGate)
        {
            if (this.disposed)
            {
                return;
            }

            this.disposed = true;
            this.requestedScene = null;
            foreach (var lifetime in this.openScenes.Values.ToArray())
            {
                this.RetireDocument(lifetime, "the preview closed");
            }

            this.activeWorld = null;
            this.activeScene = null;
            if (this.observedWorld is { } observedWorld)
            {
                observedWorld.AssetLoadFailed -= this.OnAssetLoadFailed;
                this.observedWorld = null;
            }
        }

        this.StopFollowingDocuments();
        this.disposal.Cancel();
        this.disposal.Dispose();
        this.TryDisposeSyncGate();
    }

    // A run that starts shows the requested scene; a run that ends leaves nothing ready. No state is polled.
    private void OnRuntimeStateChanged(object? sender, EngineStateChangedEventArgs args)
    {
        if (args.State == EngineServiceState.Running)
        {
            _ = this.ResumeRequestedSceneAsync(args.RunId);
            return;
        }

        int count;
        lock (this.documentGate)
        {
            count = this.ready.Count;
            foreach (var lifetime in this.openScenes.Values)
            {
                this.SetReady(lifetime, isReady: false);
            }
        }

        if (count > 0)
        {
            this.LogReadinessCleared(args.State, count);
        }
    }

    private async Task ResumeRequestedSceneAsync(Guid runId)
    {
        // Let the startup notification finish before checking the current run.
        await Task.Yield();
        Scene scene;
        lock (this.documentGate)
        {
            if (this.disposed || this.requestedScene is not { } requested || !this.TryGetDocument(requested, out _)
                || this.engineService.WorldCommands.RunId != runId)
            {
                return;
            }

            scene = requested;
        }

        // The sync checks the requested scene again once it holds the sync gate.
        this.LogResumingScene(runId, scene);
        _ = await this.SyncSceneCoreAsync(scene, skipIfCurrent: true, CancellationToken.None).ConfigureAwait(false);
    }

    private bool IsSceneProjectionCurrent(Scene scene)
    {
        lock (this.documentGate)
        {
            return this.TryGetDocument(scene, out var lifetime)
                && lifetime.LastSynchronizedTarget is { } target && this.activeWorld?.Target == target
                && target.RunId == this.engineService.WorldCommands.RunId
                && lifetime.SnapshotBoundary is { } boundary
                && boundary.Revision == lifetime.Revision && boundary.Sequence == lifetime.Sequence
                && this.GetPendingPropertySyncCount(scene.Id) == 0;
        }
    }

    // Called under the document gate.
    private void SelectRequestedScene(Scene scene)
    {
        if (!ReferenceEquals(this.requestedScene, scene))
        {
            if (this.requestedScene is { } previousScene && this.TryGetDocument(previousScene, out var previous))
            {
                this.SetReady(previous, isReady: false);
            }

            if (this.activeWorld is { } world)
            {
                world.Commands.InvalidateScene(world.Target);
                this.coalescer.ResetScene(world.Target.SceneId);
                this.activeWorld = null;
                this.activeScene = null;
            }
        }

        this.requestedScene = scene;
    }

    private void TryDisposeSyncGate()
    {
        if (this.disposed && Volatile.Read(ref this.syncOperations) == 0
            && Interlocked.Exchange(ref this.syncGateDisposed, 1) == 0)
        {
            this.sceneSyncGate.Dispose();
        }
    }
}
