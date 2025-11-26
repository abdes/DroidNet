// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using DroidNet.Hosting.WinUI;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.EngineInterface;

namespace Oxygen.Editor.WorldEditor.Engine;

/// <summary>
/// Application-wide coordinator that keeps the native engine alive and arbitrates access to composition surfaces.
/// </summary>
public sealed partial class EngineService : IEngineService
{
    private readonly HostingContext hostingContext;
    private readonly ILogger<EngineService> logger;
    private readonly ILoggerFactory? loggerFactory;
    private readonly SemaphoreSlim initializationGate = new(1, 1);
    private readonly SemaphoreSlim leaseGate = new(1, 1);
    private readonly Dictionary<Guid, int> documentSurfaceCounts = [];
    private readonly Dictionary<ViewportSurfaceKey, ViewportSurfaceLease> activeLeases = [];

    private EngineRunner? engineRunner;
    private EngineContext? engineContext;
    private Task? engineLoopTask;
    private EngineServiceState state = EngineServiceState.Created;
    private bool disposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="EngineService"/> class.
    /// </summary>
    /// <param name="hostingContext">Provides access to the UI dispatcher context.</param>
    /// <param name="logger">The logger used for diagnostics.</param>
    /// <param name="loggerFactory">Optional factory used to bridge native engine logging.</param>
    public EngineService(HostingContext hostingContext, ILogger<EngineService> logger, ILoggerFactory? loggerFactory = null)
    {
        this.hostingContext = hostingContext;
        this.logger = logger;
        this.loggerFactory = loggerFactory;
    }

    /// <summary>
    /// Gets the current lifecycle state of the service.
    /// </summary>
    public EngineServiceState State => this.state;

    /// <summary>
    /// Gets the number of active viewport leases currently tracked by the service.
    /// </summary>
    public int ActiveSurfaceCount => this.activeLeases.Count;

    /// <inheritdoc />
    public async ValueTask EnsureInitializedAsync(CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();

        if (this.state is EngineServiceState.Ready or EngineServiceState.Running)
        {
            return;
        }

        await this.initializationGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (this.state is EngineServiceState.Ready or EngineServiceState.Running)
            {
                return;
            }

            this.state = EngineServiceState.Initializing;

            if (this.engineRunner == null)
            {
                this.engineRunner = new EngineRunner();
                this.engineRunner.CaptureUiSynchronizationContext();

                if (this.loggerFactory is { } factory)
                {
                    var loggingConfig = new LoggingConfig
                    {
                        Verbosity = 1,
                        IsColored = false,
                    };
                    var engineLogger = factory.CreateLogger("Oxygen.Engine");
                    _ = this.engineRunner.ConfigureLogging(loggingConfig, engineLogger);
                }

                this.LogRunnerInitialized();
            }

            this.EnsureEngineContext();
        }
        catch (Exception ex)
        {
            this.state = EngineServiceState.Faulted;
            this.LogInitializationFailed(ex);
            throw;
        }
        finally
        {
            this.initializationGate.Release();
        }
    }

    /// <inheritdoc />
    public async ValueTask<IViewportSurfaceLease> AttachViewportAsync(ViewportSurfaceRequest request, SwapChainPanel panel, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        ArgumentNullException.ThrowIfNull(panel);
        this.ThrowIfDisposed();
        this.EnsureOnDispatcherThread();

        await this.EnsureInitializedAsync(cancellationToken).ConfigureAwait(true);

        var key = request.ToKey();
        var lease = await this.GetOrCreateLeaseAsync(key, cancellationToken).ConfigureAwait(true);
        await lease.AttachAsync(panel, cancellationToken).ConfigureAwait(true);
        return lease;
    }

    /// <inheritdoc />
    public async ValueTask ReleaseDocumentSurfacesAsync(Guid documentId, CancellationToken cancellationToken = default)
    {
        this.ThrowIfDisposed();

        List<ViewportSurfaceLease> targetLeases;
        await this.leaseGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            targetLeases = this.activeLeases
                .Where(pair => pair.Key.DocumentId == documentId)
                .Select(pair => pair.Value)
                .ToList();
        }
        finally
        {
            this.leaseGate.Release();
        }

        foreach (var lease in targetLeases)
        {
            await lease.DisposeAsync().ConfigureAwait(true);
        }
    }

    /// <inheritdoc />
    public async ValueTask DisposeAsync()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.state = EngineServiceState.Stopping;

        List<ViewportSurfaceLease> leasesSnapshot;
        await this.leaseGate.WaitAsync().ConfigureAwait(false);
        try
        {
            leasesSnapshot = this.activeLeases.Values.ToList();
            this.activeLeases.Clear();
            this.documentSurfaceCounts.Clear();
        }
        finally
        {
            this.leaseGate.Release();
        }

        foreach (var lease in leasesSnapshot)
        {
            lease.MarkDetached();
        }

        await this.StopEngineAsync(keepContextAlive: false).ConfigureAwait(false);
        this.DestroyEngineContext();
        this.engineRunner?.Dispose();
        this.state = EngineServiceState.Created;
    }

    /// <summary>
    /// Ensures the specified lease is connected to a running engine instance.
    /// </summary>
    private async ValueTask AttachLeaseAsync(ViewportSurfaceLease lease, SwapChainPanel panel, CancellationToken cancellationToken)
    {
        if (this.engineRunner == null)
        {
            throw new InvalidOperationException("Engine runner not initialized.");
        }

        await this.EnsureInitializedAsync(cancellationToken).ConfigureAwait(true);

        if (this.engineContext == null)
        {
            throw new InvalidOperationException("Engine context is not available.");
        }

        var panelPtr = IntPtr.Zero;
        try
        {
            panelPtr = Marshal.GetIUnknownForObject(panel);
            var registered = this.engineRunner.RegisterSurface(
                this.engineContext,
                lease.Key.DocumentId,
                lease.Key.ViewportId,
                lease.Key.DisplayName,
                panelPtr);

            if (!registered)
            {
                throw new InvalidOperationException("Failed to register viewport surface with the native engine.");
            }
        }
        finally
        {
            if (panelPtr != IntPtr.Zero)
            {
                Marshal.Release(panelPtr);
            }
        }

        lease.MarkAttached(panel);
        this.LogLeaseAttached(lease.Key.DisplayName, panel.GetHashCode(), this.state);

        if (this.engineLoopTask == null)
        {
            this.LogStartingEngineLoop(panel.GetHashCode());
            this.state = EngineServiceState.Initializing;
            this.engineLoopTask = this.engineRunner.RunEngineAsync(this.engineContext);
        }

        this.state = EngineServiceState.Running;
    }

    /// <summary>
    /// Forwards a resize request to the native engine.
    /// </summary>
    private async ValueTask ResizeViewportAsync(ViewportSurfaceKey key, uint pixelWidth, uint pixelHeight, bool waitForProcessed = false)
    {
        if (pixelWidth == 0 || pixelHeight == 0)
        {
            return;
        }

        try
        {
            if (waitForProcessed && this.engineRunner != null)
            {
                await this.engineRunner.ResizeSurfaceAsync(key.ViewportId, pixelWidth, pixelHeight).ConfigureAwait(true);
            }
            else
            {
                this.engineRunner?.ResizeSurface(key.ViewportId, pixelWidth, pixelHeight);
            }
        }
        catch (ObjectDisposedException ex)
        {
            this.LogResizeFailed(ex, pixelWidth, pixelHeight);
            throw;
        }
        catch (InvalidOperationException ex)
        {
            this.LogResizeFailed(ex, pixelWidth, pixelHeight);
            throw;
        }
    }

    /// <summary>
    /// Releases the resources associated with the specified lease.
    /// </summary>
    private async ValueTask ReleaseLeaseAsync(ViewportSurfaceLease lease, bool waitForProcessed = false)
    {
        this.logger?.LogDebug("[{Timestamp}] ReleaseLeaseAsync start for {LeaseKey}", DateTimeOffset.UtcNow, lease.Key.DisplayName);
        var idleCandidate = false;
        var documentCountAfter = 0;
        var totalCountAfter = 0;

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
                    documentCountAfter = 0;
                }
                else
                {
                    this.documentSurfaceCounts[lease.Key.DocumentId] = count;
                    documentCountAfter = count;
                }
            }
            else
            {
                documentCountAfter = 0;
            }

            idleCandidate = this.activeLeases.Count == 0;
            totalCountAfter = this.activeLeases.Count;
        }
        finally
        {
            this.leaseGate.Release();
        }

        lease.MarkDetached();
        this.logger?.LogDebug("[{Timestamp}] Calling UnregisterSurface for {LeaseKey}", DateTimeOffset.UtcNow, lease.Key.DisplayName);
        if (waitForProcessed && this.engineRunner != null)
        {
            await this.engineRunner.UnregisterSurfaceAsync(lease.Key.ViewportId).ConfigureAwait(true);
        }
        else
        {
            this.engineRunner?.UnregisterSurface(lease.Key.ViewportId);
        }
        this.logger?.LogDebug("[{Timestamp}] UnregisterSurface completed for {LeaseKey}", DateTimeOffset.UtcNow, lease.Key.DisplayName);
        this.LogLeaseReleased(lease.Key.DisplayName, lease.Key.DocumentId, totalCountAfter, documentCountAfter, idleCandidate);
        this.logger?.LogDebug("[{Timestamp}] ReleaseLeaseAsync completed for {LeaseKey}", DateTimeOffset.UtcNow, lease.Key.DisplayName);
    }

    private async ValueTask<ViewportSurfaceLease> GetOrCreateLeaseAsync(ViewportSurfaceKey key, CancellationToken cancellationToken)
    {
        await this.leaseGate.WaitAsync(cancellationToken).ConfigureAwait(false);
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
            this.leaseGate.Release();
        }
    }

    private void ThrowIfSurfaceLimitExceeded(Guid documentId)
    {
        if (this.activeLeases.Count >= EngineSurfaceLimits.MaxTotalSurfaces)
        {
            throw new InvalidOperationException($"Cannot attach viewport: reached the global limit of {EngineSurfaceLimits.MaxTotalSurfaces} surfaces.");
        }

        var perDocumentCount = this.documentSurfaceCounts.TryGetValue(documentId, out var value) ? value : 0;
        if (perDocumentCount >= EngineSurfaceLimits.MaxSurfacesPerDocument)
        {
            throw new InvalidOperationException($"Document {documentId} already owns the maximum of {EngineSurfaceLimits.MaxSurfacesPerDocument} surfaces.");
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

    private int GetDocumentSurfaceCount(Guid documentId)
        => this.documentSurfaceCounts.TryGetValue(documentId, out var count) ? count : 0;

    private async ValueTask StopEngineAsync(bool keepContextAlive = true)
    {
        if (this.engineRunner == null)
        {
            this.LogStopEngineSkipped("Engine runner not initialized.");
            return;
        }

        var hasActiveContext = this.engineContext != null;
        this.LogStopEngineRequested(keepContextAlive, hasActiveContext, this.state);

        if (this.engineLoopTask == null || this.engineContext == null)
        {
            if (!keepContextAlive)
            {
                this.DestroyEngineContext();
                this.state = EngineServiceState.Created;
            }
            else if (this.engineContext?.IsValid == true)
            {
                this.state = EngineServiceState.Ready;
            }
            else
            {
                this.state = EngineServiceState.Created;
            }

            this.LogStopEngineCompleted(keepContextAlive, this.state);
            return;
        }

        this.LogStoppingEngineLoop();
        this.state = EngineServiceState.Stopping;

        try
        {
            this.engineRunner.StopEngine(this.engineContext);
            if (this.engineLoopTask != null)
            {
                await this.engineLoopTask.ConfigureAwait(false);
            }
        }
        catch (ObjectDisposedException ex)
        {
            this.LogEngineStopFault(ex);
            throw;
        }
        catch (InvalidOperationException ex)
        {
            this.LogEngineStopFault(ex);
            throw;
        }
        finally
        {
            this.engineLoopTask = null;
            if (keepContextAlive && this.engineContext != null)
            {
                this.state = EngineServiceState.Ready;
            }
            else
            {
                this.DestroyEngineContext();
                this.state = EngineServiceState.Created;
            }

            this.LogStopEngineCompleted(keepContextAlive, this.state);
        }
    }

    private void ThrowIfDisposed()
        => ObjectDisposedException.ThrowIf(this.disposed, this);

    private void EnsureOnDispatcherThread()
    {
        if (!this.hostingContext.Dispatcher.HasThreadAccess)
        {
            throw new InvalidOperationException("Engine operations must be performed on the UI dispatcher thread.");
        }
    }

    private void EnsureEngineContext()
    {
        if (this.engineRunner == null)
        {
            throw new InvalidOperationException("Engine runner not initialized.");
        }

        if (this.engineContext?.IsValid == true)
        {
            this.LogContextReused();
            this.state = EngineServiceState.Ready;
            return;
        }

        var config = ConfigFactory.CreateDefaultEngineConfig();
        config.Graphics ??= new GraphicsConfigManaged();
        config.Graphics.Headless = true;

        // TODO: temporarirly set FPS at 1 to reduce log throughput
        config.TargetFps = 1;

        this.engineContext = this.engineRunner.CreateEngine(config);
        if (this.engineContext?.IsValid != true)
        {
            throw new InvalidOperationException("Failed to create engine context.");
        }

        this.LogContextReady();
        this.state = EngineServiceState.Ready;
    }

    private void DestroyEngineContext()
    {
        if (this.engineContext == null)
        {
            return;
        }

        try
        {
            this.engineContext.Dispose();
        }
        catch (Exception ex)
        {
            this.LogInitializationFailed(ex);
        }
        finally
        {
            this.engineContext = null;
            this.LogContextDestroyed();
        }
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

            await this.owner.ResizeViewportAsync(this.Key, pixelWidth, pixelHeight, false).ConfigureAwait(true);
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
