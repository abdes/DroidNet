// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
///     Surface management partial implementation for the <see cref="EngineService" />.
/// </summary>
public partial class EngineService
{
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

        List<ViewportSurfaceLease> targetLeases;
        await this.leaseGate.WaitAsync().ConfigureAwait(false);
        try
        {
#pragma warning disable IDE0305 // Simplify collection initialization
            targetLeases = this.activeLeases
                .Where(pair => pair.Key.DocumentId == documentId)
                .Select(pair => pair.Value)
                .ToList();
#pragma warning restore IDE0305 // Simplify collection initialization
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

    private int GetDocumentSurfaceCount(Guid documentId)
        => this.documentSurfaceCounts.TryGetValue(documentId, out var count) ? count : 0;

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

            if (!await runner.RegisterSurfaceAsync(
                this.engineContext,
                lease.Key.DocumentId,
                lease.Key.ViewportId,
                lease.Key.DisplayName,
                panelPtr,
                initialPixelWidth,
                initialPixelHeight,
                raster).ConfigureAwait(true))
            {
                throw new InvalidOperationException("Failed to register viewport surface with the native engine.");
            }

            if (cancellationToken.IsCancellationRequested)
            {
                await TryRollBack(lease, runner).ConfigureAwait(false);
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

        static async Task TryRollBack(ViewportSurfaceLease lease, Interop.EngineRunner runner)
        {
            try
            {
                _ = await runner.UnregisterSurfaceAsync(lease.Key.ViewportId).ConfigureAwait(false);
            }
#pragma warning disable CA1031 // Do not catch general exception types
            catch
            {
                // Best-effort cleanup; swallow to allow throwing OperationCanceledException below.
            }
#pragma warning restore CA1031 // Do not catch general exception types
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
            var result = await runner.ResizeSurfaceAsync(key.ViewportId, pixelWidth, pixelHeight).ConfigureAwait(true);
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

        await this.leaseGate.WaitAsync().ConfigureAwait(false);
        try
        {
            if (!this.activeLeases.Remove(lease.Key))
            {
                return;
            }

            if (this.documentSurfaceCounts.TryGetValue(lease.Key.DocumentId, out var count))
            {
                count--;
                if (count <= 0)
                {
                    _ = this.documentSurfaceCounts.Remove(lease.Key.DocumentId);
                }
                else
                {
                    this.documentSurfaceCounts[lease.Key.DocumentId] = count;
                }
            }
        }
        finally
        {
            _ = this.leaseGate.Release();
        }

        // FIXME: Consider waiting for the unregister to complete before marking detached and updating the collections
        lease.MarkDetached();
        var result = await runner.UnregisterSurfaceAsync(lease.Key.ViewportId).ConfigureAwait(false);
        this.LogLeaseReleased(lease.Key.DisplayName, lease.Key.DocumentId, result);
    }

    private async ValueTask<ViewportSurfaceLease> GetOrCreateLeaseAsync(ViewportSurfaceKey key)
    {
        await this.leaseGate.WaitAsync().ConfigureAwait(false);
        try
        {
            if (this.activeLeases.TryGetValue(key, out var existing))
            {
                this.LogLeaseReused(key.DisplayName, key.DocumentId, this.activeLeases.Count, this.GetDocumentSurfaceCount(key.DocumentId));
                return existing;
            }

            this.ThrowIfSurfaceLimitExceeded(key.DocumentId);
            var lease = new ViewportSurfaceLease(this, key);
            this.activeLeases.Add(key, lease);
            var documentCount = this.IncrementDocumentSurfaceCount(key.DocumentId);
            this.LogLeaseCreated(key.DisplayName, key.DocumentId, this.activeLeases.Count, documentCount);
            return lease;
        }
        finally
        {
            _ = this.leaseGate.Release();
        }
    }

    private void ThrowIfSurfaceLimitExceeded(Guid documentId)
    {
        if (this.activeLeases.Count >= EngineConstants.MaxTotalSurfaces)
        {
            throw new InvalidOperationException($"Cannot attach viewport: reached the global limit of {EngineConstants.MaxTotalSurfaces} surfaces.");
        }

        var perDocumentCount = this.documentSurfaceCounts.TryGetValue(documentId, out var value) ? value : 0;
        if (perDocumentCount >= EngineConstants.MaxSurfacesPerDocument)
        {
            throw new InvalidOperationException($"Document {documentId} already owns the maximum of {EngineConstants.MaxSurfacesPerDocument} surfaces.");
        }
    }

    private int IncrementDocumentSurfaceCount(Guid documentId)
    {
        if (this.documentSurfaceCounts.TryGetValue(documentId, out var count))
        {
            count++;
            this.documentSurfaceCounts[documentId] = count;
            return count;
        }

        this.documentSurfaceCounts[documentId] = 1;
        return 1;
    }

    private sealed class ViewportSurfaceLease : IViewportSurfaceLease
    {
        private readonly EngineService owner;
        private SwapChainPanel? panel;
        private bool disposed;

        internal ViewportSurfaceLease(EngineService owner, ViewportSurfaceKey key)
        {
            this.owner = owner;
            this.Key = key;
        }

        public ViewportSurfaceKey Key { get; }

        public bool IsAttached => this.panel != null;

        public async ValueTask AttachAsync(SwapChainPanel panel, CancellationToken cancellationToken = default)
        {
            ArgumentNullException.ThrowIfNull(panel);
            this.ThrowIfDisposed();
            await this.owner.AttachLeaseAsync(this, panel, cancellationToken).ConfigureAwait(true);
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
            await this.owner.ReleaseLeaseAsync(this).ConfigureAwait(false);
        }

        internal void MarkAttached(SwapChainPanel panel) => this.panel = panel;

        internal void MarkDetached() => this.panel = null;

        private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(this.disposed, this);
    }
}
