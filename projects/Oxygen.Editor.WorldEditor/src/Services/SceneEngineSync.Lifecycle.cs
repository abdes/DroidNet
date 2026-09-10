// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.World.Services;

/// <summary>Resumes the requested scene on a new runtime run and retires asynchronous ownership safely.</summary>
public sealed partial class SceneEngineSync
{
    private Scene? requestedScene;
    private volatile bool disposed;
    private bool observingEngine;
    private int syncOperations;
    private int syncGateDisposed;

    /// <inheritdoc/>
    public event EventHandler<SceneSynchronizationCompletedEventArgs>? SceneSynchronized;

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
            if (this.observingEngine)
            {
                this.engineService.StateChanged -= this.OnRuntimeStateChanged;
                this.observingEngine = false;
            }

            foreach (var lifetime in this.openScenes.Values.ToArray())
            {
                this.RetireDocument(lifetime);
            }

            this.activeWorld = null;
            this.activeScene = null;
            if (this.observedWorld is { } observedWorld)
            {
                observedWorld.AssetLoadFailed -= this.OnAssetLoadFailed;
                this.observedWorld = null;
            }
        }

        this.TryDisposeSyncGate();
    }

    private void OnRuntimeStateChanged(object? sender, EngineStateChangedEventArgs args)
    {
        if (args.State == EngineServiceState.Running)
        {
            _ = this.ResumeRequestedSceneAsync(args.RunId);
        }
    }

    private async Task ResumeRequestedSceneAsync(Guid runId)
    {
        // Let the startup notification finish before checking the current run.
        await Task.Yield();
        Task<bool> sync;
        lock (this.documentGate)
        {
            if (this.disposed || this.requestedScene is not { } scene || !this.TryGetDocument(scene, out _)
                || this.engineService.WorldCommands.RunId != runId)
            {
                return;
            }

            sync = this.SyncSceneCoreAsync(scene, skipIfCurrent: true, CancellationToken.None);
        }

        _ = await sync.ConfigureAwait(false);
    }

    private bool IsSceneProjectionCurrent(Scene scene)
    {
        lock (this.documentGate)
        {
            return this.TryGetDocument(scene, out var lifetime)
                && lifetime.LastSynchronizedTarget is { } target && this.activeWorld?.Target == target
                && target.RunId == this.engineService.WorldCommands.RunId
                && lifetime.SnapshotBoundary is { } boundary
                && boundary.Revision == lifetime.Metadata.ChangeVersion && boundary.Sequence == lifetime.Sequence
                && this.GetPendingPropertySyncCount(scene.Id) == 0;
        }
    }

    private void SelectRequestedScene(Scene scene)
    {
        if (!ReferenceEquals(this.requestedScene, scene) && this.activeWorld is { } previous)
        {
            previous.Commands.InvalidateScene(previous.Target);
            this.coalescer.ResetScene(previous.Target.SceneId);
            this.activeWorld = null;
            this.activeScene = null;
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
