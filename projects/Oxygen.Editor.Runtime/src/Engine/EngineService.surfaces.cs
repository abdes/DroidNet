// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Globalization;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Serializes surface operations with engine teardown.</summary>
public sealed partial class EngineService
{
    private readonly ConcurrentDictionary<Guid, byte> orphanedViewportIds = new();

    /// <inheritdoc/>
    public ValueTask<IViewportSurfaceLease> AttachViewportAsync(ViewportSurfaceRequest request, SwapChainPanel panel, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(panel);
        this.EnsureOnDispatcherThread();
        return this.AttachViewportCoreAsync(request.ToKey(), panel, cancellationToken);
    }

    /// <inheritdoc/>
    public async ValueTask ReleaseDocumentSurfacesAsync(Guid documentId)
    {
        await this.lifecycleGate.WaitAsync().ConfigureAwait(true);
        try
        {
            List<Exception> failures = [];
            foreach (var lease in this.activeLeases.Values.Where(lease => lease.Key.DocumentId == documentId).ToArray())
            {
                _ = await this.TryCleanupAsync(() => this.ReleaseLeaseCoreAsync(lease), "Release document surface", failures).ConfigureAwait(true);
            }

            ThrowCleanupFailures(failures);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <summary>Attaches a surface under the lifecycle gate after dispatcher validation.</summary>
    /// <param name="key">The surface identity.</param>
    /// <param name="panel">The composition panel.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The tracked surface lease.</returns>
    internal async ValueTask<IViewportSurfaceLease> AttachViewportCoreAsync(ViewportSurfaceKey key, SwapChainPanel panel, CancellationToken cancellationToken = default)
    {
        await this.lifecycleGate.WaitAsync(cancellationToken).ConfigureAwait(true);
        try
        {
            this.EnsureInStates(EngineServiceState.Running);
            var lease = this.GetOrCreateLease(key);
            if (!lease.IsAttached)
            {
                await this.AttachLeaseCoreAsync(lease, panel, cancellationToken).ConfigureAwait(true);
            }

            return lease;
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    private async Task AttachLeaseCoreAsync(ViewportSurfaceLease lease, SwapChainPanel panel, CancellationToken cancellationToken)
    {
        lease.NeedsUnregister = true;
        try
        {
            var registered = await this.AwaitRuntimeOperationAsync(this.session!.RegisterSurfaceAsync(lease.Key, panel)).ConfigureAwait(true);
            if (!registered)
            {
                lease.NeedsUnregister = false;
                this.RemoveLeaseReservation(lease);
                throw new InvalidOperationException("Failed to register viewport surface with the native engine.");
            }

            if (cancellationToken.IsCancellationRequested)
            {
                List<Exception> failures = [];
                _ = await this.TryCleanupAsync(() => this.ReleaseLeaseCoreAsync(lease), "Cancel surface registration", failures).ConfigureAwait(true);
                cancellationToken.ThrowIfCancellationRequested();
            }

            lease.MarkAttached();
        }
        catch
        {
            if (lease.NeedsUnregister)
            {
                _ = this.orphanedViewportIds.TryAdd(lease.Key.ViewportId, 0);
            }

            throw;
        }
    }

    private async Task<T> AwaitRuntimeOperationAsync<T>(Task<T> operation)
    {
        var loop = this.engineLoopTask;
        if (loop is not null && await Task.WhenAny(operation, loop).ConfigureAwait(true) == loop && !operation.IsCompleted)
        {
            _ = this.ObserveAbandonedSurfaceOperationAsync(operation);
            throw new InvalidOperationException("The engine loop exited before the surface operation completed.");
        }

        return await operation.ConfigureAwait(true);
    }

    private async Task ObserveAbandonedSurfaceOperationAsync<T>(Task<T> operation)
    {
        List<Exception> failures = [];
        _ = await this.TryCleanupAsync(async () => _ = await operation.ConfigureAwait(false), "Complete interrupted surface operation", failures).ConfigureAwait(false);
    }

    private async ValueTask ResizeLeaseAsync(ViewportSurfaceLease lease, uint width, uint height, CancellationToken cancellationToken)
    {
        await this.lifecycleGate.WaitAsync(cancellationToken).ConfigureAwait(true);
        try
        {
            if (!lease.IsAttached || width == 0 || height == 0)
            {
                return;
            }

            this.EnsureInStates(EngineServiceState.Running);
            if (!await this.AwaitRuntimeOperationAsync(this.session!.ResizeSurfaceAsync(lease.Key.ViewportId, width, height)).ConfigureAwait(true))
            {
                this.LogResizeFailed(width, height);
            }
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    private async ValueTask ReleaseLeaseAsync(ViewportSurfaceLease lease)
    {
        await this.lifecycleGate.WaitAsync().ConfigureAwait(true);
        try
        {
            List<Exception> failures = [];
            _ = await this.TryCleanupAsync(() => this.ReleaseLeaseCoreAsync(lease), "Dispose surface lease", failures).ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    private async Task ReleaseLeaseCoreAsync(ViewportSurfaceLease lease)
    {
        if (!this.activeLeases.TryGetValue(lease.Key, out var current) || !ReferenceEquals(current, lease))
        {
            return;
        }

        if (lease.NeedsUnregister)
        {
            if (this.engineLoopTask?.IsCompleted != false)
            {
                // Native runner destruction owns cleanup once its frame loop has exited.
                return;
            }

            try
            {
                if (!await this.AwaitRuntimeOperationAsync(this.session!.UnregisterSurfaceAsync(lease.Key.ViewportId)).ConfigureAwait(true))
                {
                    throw new InvalidOperationException(string.Create(CultureInfo.InvariantCulture, $"Native surface release failed for {lease.Key.ViewportId}."));
                }
            }
            catch
            {
                _ = this.orphanedViewportIds.TryAdd(lease.Key.ViewportId, 0);
                throw;
            }
        }

        this.RemoveLeaseReservation(lease);
        lease.MarkReleased();
    }

    private ViewportSurfaceLease GetOrCreateLease(ViewportSurfaceKey key)
    {
        if (this.orphanedViewportIds.ContainsKey(key.ViewportId))
        {
            throw new InvalidOperationException(string.Create(CultureInfo.InvariantCulture, $"Viewport {key.ViewportId} still requires native cleanup."));
        }

        if (this.activeLeases.TryGetValue(key, out var existing))
        {
            return existing;
        }

        if (this.reservedSurfaceCount >= EngineConstants.MaxTotalSurfaces || this.GetDocumentSurfaceCount(key.DocumentId) >= EngineConstants.MaxSurfacesPerDocument)
        {
            throw new InvalidOperationException("The maximum number of viewport surfaces has been reached.");
        }

        var lease = new ViewportSurfaceLease(this, key);
        this.activeLeases[key] = lease;
        this.documentSurfaceCounts[key.DocumentId] = this.GetDocumentSurfaceCount(key.DocumentId) + 1;
        ++this.reservedSurfaceCount;
        return lease;
    }

    private int GetDocumentSurfaceCount(Guid documentId)
        => this.documentSurfaceCounts.TryGetValue(documentId, out var count) ? count : 0;

    private void RemoveLeaseReservation(ViewportSurfaceLease lease)
    {
        if (!this.activeLeases.TryRemove(lease.Key, out _))
        {
            return;
        }

        var count = this.GetDocumentSurfaceCount(lease.Key.DocumentId) - 1;
        if (count == 0)
        {
            _ = this.documentSurfaceCounts.TryRemove(lease.Key.DocumentId, out _);
        }
        else
        {
            this.documentSurfaceCounts[lease.Key.DocumentId] = count;
        }

        --this.reservedSurfaceCount;
    }

    private sealed class ViewportSurfaceLease(EngineService owner, ViewportSurfaceKey key) : IViewportSurfaceLease
    {
        private bool releaseRequested;

        public ViewportSurfaceKey Key { get; } = key;

        public bool IsAttached { get; private set; }

        internal bool NeedsUnregister { get; set; }

        public async ValueTask AttachAsync(SwapChainPanel panel, CancellationToken cancellationToken = default)
        {
            ObjectDisposedException.ThrowIf(this.releaseRequested, this);
            _ = await owner.AttachViewportAsync(new ViewportSurfaceRequest { DocumentId = this.Key.DocumentId, ViewportId = this.Key.ViewportId, ViewportIndex = 0 }, panel, cancellationToken).ConfigureAwait(true);
        }

        public ValueTask ResizeAsync(uint pixelWidth, uint pixelHeight, CancellationToken cancellationToken = default)
            => this.releaseRequested ? ValueTask.CompletedTask : owner.ResizeLeaseAsync(this, pixelWidth, pixelHeight, cancellationToken);

        public ValueTask DisposeAsync()
        {
            this.releaseRequested = true;
            return owner.ReleaseLeaseAsync(this);
        }

        internal void MarkAttached() => this.IsAttached = true;

        internal void MarkReleased()
        {
            this.NeedsUnregister = false;
            this.IsAttached = false;
            this.releaseRequested = true;
        }
    }
}
