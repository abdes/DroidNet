// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Runtime.InteropServices;
using DroidNet.Hosting.WinUI;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
///     Surface management partial implementation for the <see cref="EngineService" />.
/// </summary>
public partial class EngineService
{
    // Blacklist of viewport ids that failed native cleanup — never reuse these ids.
    private readonly ConcurrentDictionary<Guid, byte> orphanedViewportIds = new();

    /// <inheritdoc />
    public async ValueTask<IViewportSurfaceLease> AttachViewportAsync(ViewportSurfaceRequest request, SwapChainPanel panel, CancellationToken cancellationToken = default)
    {
        _ = this.EnsureIsRunning();
        this.EnsureOnDispatcherThread();

        var key = request.ToKey();
        var lease = await this.GetOrCreateLeaseAsync(key).ConfigureAwait(true);
        await lease.AttachAsync(panel, cancellationToken).ConfigureAwait(true);
        return lease;
    }

    /// <inheritdoc />
    public async ValueTask ReleaseDocumentSurfacesAsync(Guid documentId)
    {
        _ = this.EnsureIsRunning();

        // Snapshot leases for this document without holding the lease gate while disposing
        List<ViewportSurfaceLease> targetLeases;
        await this.leaseGate.WaitAsync().ConfigureAwait(false);
        try
        {
            targetLeases = this.activeLeases
                .Where(pair => pair.Key.DocumentId == documentId)
                .Select(pair => pair.Value)
                .ToList();
        }
        finally
        {
            _ = this.leaseGate.Release();
        }

        foreach (var lease in targetLeases)
        {
            await lease.DisposeAsync().ConfigureAwait(false);
        }
    }

    private async ValueTask AttachLeaseAsync(ViewportSurfaceLease lease, SwapChainPanel panel, CancellationToken cancellationToken)
    {
        this.EnsureOnDispatcherThread();
        var runner = this.EnsureIsRunning();

        // Observe cancellation early
        cancellationToken.ThrowIfCancellationRequested();

        var panelPtr = IntPtr.Zero;
        try
        {
            panelPtr = Marshal.GetIUnknownForObject(panel);

            // IMPORTANT: We pass the 'raster' (CompositionScale) to the engine so it can apply an inverse scale
            // transform to the SwapChain. This prevents WinUI from double-scaling the content on high DPI screens,
            // which causes truncation. See EngineRunner.cpp for the implementation of the inverse transform.
            var (raster, initialPixelHeight, initialPixelWidth) = ComputeInitialDimenstions(panel);

            // Check cancellation again before issuing native call.
            cancellationToken.ThrowIfCancellationRequested();

            var registered = await runner.TryRegisterSurfaceAsync(
                this.engineContext,
                lease.Key.DocumentId,
                lease.Key.ViewportId,
                lease.Key.DisplayName,
                panelPtr,
                initialPixelWidth,
                initialPixelHeight,
                raster).ConfigureAwait(true);

            if (!registered)
            {
                // Remove the reservation we created earlier and signal failure to caller.
                await this.RemoveLeaseReservationAsync(lease).ConfigureAwait(false);
                throw new InvalidOperationException("Failed to register viewport surface with the native engine.");
            }

            if (cancellationToken.IsCancellationRequested)
            {
                _ = await TryRollBack(lease, runner).ConfigureAwait(false);
                throw new OperationCanceledException(cancellationToken);
            }
        }
        finally
        {
            if (panelPtr != IntPtr.Zero)
            {
                _ = Marshal.Release(panelPtr);
            }
        }

        // Mark attached on UI thread (we are already on dispatcher) and log.
        lease.MarkAttached(panel);
        this.LogLeaseAttached(lease.Key.DisplayName, panel.GetHashCode(), this.state);

        static (float raster, uint initialPixelHeight, uint initialPixelWidth) ComputeInitialDimenstions(SwapChainPanel panel)
        {
            var raster = (float)(panel.XamlRoot?.RasterizationScale ?? 1.0);
            uint initialPixelWidth;
            uint initialPixelHeight;

            var aw = panel.ActualWidth;
            var ah = panel.ActualHeight;
            if (!double.IsFinite(aw) || !double.IsFinite(ah) || aw <= 0.0 || ah <= 0.0)
            {
                // Invalid/unready measurements — fall back to zero so native decides
                initialPixelWidth = 0u;
                initialPixelHeight = 0u;
                raster = 1.0f;
            }
            else
            {
                var wPx = Math.Round(aw * raster);
                var hPx = Math.Round(ah * raster);

                if (!double.IsFinite(wPx) || !double.IsFinite(hPx) || wPx < 1.0 || hPx < 1.0)
                {
                    initialPixelWidth = 0u;
                    initialPixelHeight = 0u;
                    raster = 1.0f;
                }
                else
                {
                    // clamp into uint range before conversion
                    initialPixelWidth = Convert.ToUInt32(Math.Min((double)uint.MaxValue, Math.Max(1.0, wPx)));
                    initialPixelHeight = Convert.ToUInt32(Math.Min((double)uint.MaxValue, Math.Max(1.0, hPx)));
                }
            }

            return (raster, initialPixelHeight, initialPixelWidth);
        }

        async Task<bool> TryRollBack(ViewportSurfaceLease lease, Interop.EngineRunner runner)
        {
            var r = await runner.TryUnregisterSurfaceAsync(lease.Key.ViewportId).ConfigureAwait(false);
            if (!r)
            {
                _ = this.orphanedViewportIds.TryAdd(lease.Key.ViewportId, 0);
                lease.IsOrphaned = true;
            }

            return r;
        }
    }

    /// <summary>
    /// Forwards a resize request to the native engine.
    /// </summary>
    private async ValueTask ResizeViewportAsync(ViewportSurfaceKey key, uint pixelWidth, uint pixelHeight)
    {
        var runner = this.EnsureIsRunning();

        if (pixelWidth == 0 || pixelHeight == 0)
        {
            return;
        }

        try
        {
            var result = await runner.TryResizeSurfaceAsync(key.ViewportId, pixelWidth, pixelHeight).ConfigureAwait(true);
            if (!result)
            {
                this.LogResizeFailed(pixelWidth, pixelHeight);
            }
        }
        catch (Exception ex)
        {
            this.LogResizeFailed(pixelWidth, pixelHeight, ex);
            throw;
        }
    }

    /// <summary>
    /// Releases the resources associated with the specified lease.
    /// </summary>
    private async ValueTask ReleaseLeaseAsync(ViewportSurfaceLease lease)
    {
        var runner = this.EnsureIsRunning();

        // Attempt to unregister the native surface first, then remove the reservation
        // and mark detached on the UI thread. This avoids leaving a native surface registered
        // while the managed state says detached.
        var result = await runner.TryUnregisterSurfaceAsync(lease.Key.ViewportId).ConfigureAwait(true);

        if (!result)
        {
            // Blacklist this viewport id so it will never be reused by GetOrCreateLeaseAsync.
            _ = this.orphanedViewportIds.TryAdd(lease.Key.ViewportId, 0);
            lease.IsOrphaned = true;
        }

        await this.leaseGate.WaitAsync().ConfigureAwait(false);
        try
        {
            _ = this.activeLeases.TryRemove(lease.Key, out _);

            // decrement per-document reservation counts
            if (this.documentSurfaceCounts.TryGetValue(lease.Key.DocumentId, out var count))
            {
                var newCount = Math.Max(0, count - 1);
                if (newCount == 0)
                {
                    _ = this.documentSurfaceCounts.TryRemove(lease.Key.DocumentId, out _);
                }
                else
                {
                    this.documentSurfaceCounts[lease.Key.DocumentId] = newCount;
                }
            }

            // Decrement global reservation count for the removed lease.
            _ = Interlocked.Decrement(ref this.reservedSurfaceCount);
        }
        finally
        {
            _ = this.leaseGate.Release();
        }

        await this.hostingContext.Dispatcher.DispatchAsync(lease.MarkDetached).ConfigureAwait(false);

        this.LogLeaseReleased(lease.Key.DisplayName, lease.Key.DocumentId, result);
    }

    private async ValueTask<ViewportSurfaceLease> GetOrCreateLeaseAsync(ViewportSurfaceKey key)
    {
        await this.leaseGate.WaitAsync().ConfigureAwait(false);
        try
        {
            // Prevent creating or returning leases for blacklisted viewport ids.
            if (this.orphanedViewportIds.ContainsKey(key.ViewportId))
            {
                throw new InvalidOperationException($"Viewport id {key.ViewportId} is blacklisted and cannot be reused.");
            }

            if (this.activeLeases.TryGetValue(key, out var existing))
            {
                // If an existing lease is marked orphaned, do not return it.
                if (existing.IsOrphaned || this.orphanedViewportIds.ContainsKey(key.ViewportId))
                {
                    throw new InvalidOperationException($"Existing lease for viewport id {key.ViewportId} is blacklisted and cannot be reused.");
                }

                this.LogLeaseReused(key.DisplayName, key.DocumentId, this.activeLeases.Count, this.GetDocumentSurfaceCount(key.DocumentId));
                return existing;
            }

            // Check and reserve a slot for this document/global before creating the lease.
            this.ThrowIfSurfaceLimitExceeded(key.DocumentId);

            var lease = new ViewportSurfaceLease(this, key);
            if (this.activeLeases.TryAdd(key, lease))
            {
                // increment per-document reservation count
                _ = this.documentSurfaceCounts.AddOrUpdate(key.DocumentId, 1, (_, old) => old + 1);
                _ = Interlocked.Increment(ref this.reservedSurfaceCount);
                var documentCount = this.GetDocumentSurfaceCount(key.DocumentId);
                this.LogLeaseCreated(key.DisplayName, key.DocumentId, this.activeLeases.Count, documentCount);
                return lease;
            }

            // If another thread added in the meantime, return that one
            _ = this.activeLeases.TryGetValue(key, out var raced);
            return raced!;
        }
        finally
        {
            _ = this.leaseGate.Release();
        }
    }

    private void ThrowIfSurfaceLimitExceeded(Guid documentId)
    {
        // Use reservedSurfaceCount and documentSurfaceCounts which are updated under leaseGate
        if (this.reservedSurfaceCount >= EngineConstants.MaxTotalSurfaces)
        {
            throw new InvalidOperationException($"Cannot attach viewport: reached the global limit of {EngineConstants.MaxTotalSurfaces} surfaces.");
        }

        var perDocumentCount = this.documentSurfaceCounts.TryGetValue(documentId, out var value) ? value : 0;
        if (perDocumentCount >= EngineConstants.MaxSurfacesPerDocument)
        {
            throw new InvalidOperationException($"Document {documentId} already owns the maximum of {EngineConstants.MaxSurfacesPerDocument} surfaces.");
        }
    }

    private int GetDocumentSurfaceCount(Guid documentId)
        => this.documentSurfaceCounts.TryGetValue(documentId, out var count) ? count : 0;

    private async ValueTask RemoveLeaseReservationAsync(ViewportSurfaceLease lease)
    {
        // Remove reservation created in GetOrCreateLeaseAsync when attach fails
        await this.leaseGate.WaitAsync().ConfigureAwait(false);
        try
        {
            _ = this.activeLeases.TryRemove(lease.Key, out _);
            if (this.documentSurfaceCounts.TryGetValue(lease.Key.DocumentId, out var count))
            {
                var newCount = Math.Max(0, count - 1);
                if (newCount == 0)
                {
                    _ = this.documentSurfaceCounts.TryRemove(lease.Key.DocumentId, out _);
                }
                else
                {
                    this.documentSurfaceCounts[lease.Key.DocumentId] = newCount;
                }
            }

            _ = Interlocked.Decrement(ref this.reservedSurfaceCount);
        }
        finally
        {
            _ = this.leaseGate.Release();
        }
    }

    private sealed class ViewportSurfaceLease : IViewportSurfaceLease
    {
        private readonly EngineService owner;
        private readonly SemaphoreSlim leaseLock = new(1, 1);

        private SwapChainPanel? panel;
        private bool disposed;

        internal ViewportSurfaceLease(EngineService owner, ViewportSurfaceKey key)
        {
            this.owner = owner;
            this.Key = key;
        }

        public ViewportSurfaceKey Key { get; }

        public bool IsAttached => this.panel != null;

        // When true the lease's viewport id is known-bad and must never be reused.
        internal bool IsOrphaned { get; set; }

        public async ValueTask AttachAsync(SwapChainPanel panel, CancellationToken cancellationToken = default)
        {
            ArgumentNullException.ThrowIfNull(panel);
            this.ThrowIfDisposed();
            await this.leaseLock.WaitAsync(cancellationToken).ConfigureAwait(true);
            try
            {
                // If already attached to the same panel, no-op
                if (this.IsAttached)
                {
                    return;
                }

                await this.owner.AttachLeaseAsync(this, panel, cancellationToken).ConfigureAwait(true);
            }
            finally
            {
                _ = this.leaseLock.Release();
            }
        }

        public async ValueTask ResizeAsync(uint pixelWidth, uint pixelHeight, CancellationToken cancellationToken = default)
        {
            if (!this.IsAttached)
            {
                return;
            }

            await this.owner.ResizeViewportAsync(this.Key, pixelWidth, pixelHeight).ConfigureAwait(true);
            return;
        }

        public async ValueTask DisposeAsync()
        {
            if (this.disposed)
            {
                return;
            }

            this.disposed = true;
            await this.leaseLock.WaitAsync().ConfigureAwait(true);
            try
            {
                // Ensure Dispose never throws: ReleaseLeaseAsync may touch native code
                // and can throw; swallow and log here to respect the Dispose contract.
                await this.owner.ReleaseLeaseAsync(this).ConfigureAwait(false);
            }
            finally
            {
                _ = this.leaseLock.Release();
                this.leaseLock.Dispose();
            }
        }

        internal void MarkAttached(SwapChainPanel panel) => this.panel = panel;

        internal void MarkDetached() => this.panel = null;

        private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(this.disposed, this);
    }
}
