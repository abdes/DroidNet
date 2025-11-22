// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
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
    private readonly Dictionary<Guid, int> documentSurfaceCounts = new();
    private readonly Dictionary<ViewportSurfaceKey, ViewportSurfaceLease> activeLeases = new();

    private EngineRunner? engineRunner;
    private EngineContext? dormantContext;
    private EngineContext? engineContext;
    private Task? engineLoopTask;
    private EngineServiceState state = EngineServiceState.Created;
    private ViewportSurfaceKey? primaryLeaseKey;
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

        if (this.state is EngineServiceState.Dormant or EngineServiceState.Running)
        {
            return;
        }

        await this.initializationGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (this.state is EngineServiceState.Dormant or EngineServiceState.Running)
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
                        Verbosity = 0,
                        IsColored = false,
                    };
                    var engineLogger = factory.CreateLogger("Oxygen.Engine");
                    _ = this.engineRunner.ConfigureLogging(loggingConfig, engineLogger);
                }

                this.LogRunnerInitialized();
            }

            this.EnsureDormantContext();
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
            this.primaryLeaseKey = null;
        }
        finally
        {
            this.leaseGate.Release();
        }

        foreach (var lease in leasesSnapshot)
        {
            lease.MarkDetached();
        }

        await this.StopEngineAsync(transitionToDormant: false).ConfigureAwait(false);
        this.DestroyDormantContext();
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

        if (this.engineContext == null)
        {
            await this.StartEngineForPanelAsync(panel, cancellationToken).ConfigureAwait(true);
            this.primaryLeaseKey = lease.Key;
            this.LogPrimaryLeaseSelected(lease.Key.DisplayName);
            lease.MarkAttached(panel);
            this.LogLeaseAttached(lease.Key.DisplayName, panel.GetHashCode(), this.state);
            return;
        }

        if (this.primaryLeaseKey.HasValue && this.primaryLeaseKey != lease.Key)
        {
            throw new NotSupportedException("Multiple viewport attachments are not supported yet. Native surface multiplexing is pending.");
        }

        lease.MarkAttached(panel);
        this.LogLeaseAttached(lease.Key.DisplayName, panel.GetHashCode(), this.state);
    }

    /// <summary>
    /// Forwards a resize request to the native engine.
    /// </summary>
    private void ResizeViewport(uint pixelWidth, uint pixelHeight)
    {
        if (pixelWidth == 0 || pixelHeight == 0)
        {
            return;
        }

        try
        {
            this.engineRunner?.ResizeViewport(pixelWidth, pixelHeight);
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
    private async ValueTask ReleaseLeaseAsync(ViewportSurfaceLease lease)
    {
        var shouldStopEngine = false;
        var wasPrimaryLease = false;
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

            if (this.primaryLeaseKey == lease.Key)
            {
                this.primaryLeaseKey = null;
                wasPrimaryLease = true;
            }

            shouldStopEngine = this.activeLeases.Count == 0;
            totalCountAfter = this.activeLeases.Count;
        }
        finally
        {
            this.leaseGate.Release();
        }

        lease.MarkDetached();
        this.LogLeaseReleased(lease.Key.DisplayName, lease.Key.DocumentId, totalCountAfter, documentCountAfter, wasPrimaryLease, shouldStopEngine);

        if (shouldStopEngine)
        {
            this.engineRunner?.ReleaseEditorSurface();
            await this.StopEngineAsync().ConfigureAwait(false);
        }
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

    private async ValueTask StartEngineForPanelAsync(SwapChainPanel panel, CancellationToken cancellationToken)
    {
        if (this.engineRunner == null)
        {
            throw new InvalidOperationException("Engine runner not initialized.");
        }

        this.LogStartingEngineLoop(panel.GetHashCode());
        this.DestroyDormantContext();
        this.state = EngineServiceState.Initializing;

        var config = ConfigFactory.CreateDefaultEngineConfig();
        var panelPtr = IntPtr.Zero;

        try
        {
            panelPtr = Marshal.GetIUnknownForObject(panel);
            this.engineContext = this.engineRunner.CreateEngine(config, panelPtr);
        }
        finally
        {
            if (panelPtr != IntPtr.Zero)
            {
                Marshal.Release(panelPtr);
            }
        }

        if (this.engineContext == null || !this.engineContext.IsValid)
        {
            throw new InvalidOperationException("Native engine context creation failed.");
        }

        this.engineLoopTask = this.engineRunner.RunEngineAsync(this.engineContext);
        this.state = EngineServiceState.Running;
    }

    private async ValueTask StopEngineAsync(bool transitionToDormant = true)
    {
        if (this.engineRunner == null)
        {
            this.LogStopEngineSkipped("Engine runner not initialized.");
            return;
        }

        var hasActiveContext = this.engineContext != null;
        this.LogStopEngineRequested(transitionToDormant, hasActiveContext, this.state);

        if (!hasActiveContext)
        {
            if (!transitionToDormant)
            {
                this.DestroyDormantContext();
            }

            this.state = EngineServiceState.Created;
            this.LogStopEngineCompleted(transitionToDormant, this.state);
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
            this.engineContext = null;

            if (!transitionToDormant)
            {
                this.DestroyDormantContext();
            }

            this.state = EngineServiceState.Created;
            this.LogStopEngineCompleted(transitionToDormant, this.state);
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

    private void EnsureDormantContext()
    {
        if (this.engineRunner == null)
        {
            throw new InvalidOperationException("Engine runner not initialized.");
        }

        if (this.dormantContext != null && this.dormantContext.IsValid)
        {
            this.LogDormantContextReused();
            this.state = EngineServiceState.Dormant;
            return;
        }

        var config = ConfigFactory.CreateDefaultEngineConfig();
        config.Graphics ??= new GraphicsConfigManaged();
        config.Graphics.Headless = true;

        this.dormantContext = this.engineRunner.CreateEngine(config);
        if (this.dormantContext == null || !this.dormantContext.IsValid)
        {
            throw new InvalidOperationException("Failed to create dormant engine context.");
        }

        this.LogDormantContextReady();
        this.state = EngineServiceState.Dormant;
    }

    private void DestroyDormantContext()
    {
        if (this.dormantContext == null)
        {
            return;
        }

        try
        {
            this.dormantContext?.Dispose();
        }
        catch (Exception ex)
        {
            this.LogInitializationFailed(ex);
        }
        finally
        {
            this.dormantContext = null;
            this.LogDormantContextDestroyed();
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

        public ValueTask ResizeAsync(uint pixelWidth, uint pixelHeight, CancellationToken cancellationToken = default)
        {
            if (!this.IsAttached)
            {
                return ValueTask.CompletedTask;
            }

            this.owner.ResizeViewport(pixelWidth, pixelHeight);
            return ValueTask.CompletedTask;
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

        internal void MarkAttached(SwapChainPanel panel)
        {
            this.panel = panel;
        }

        internal void MarkDetached()
        {
            this.panel = null;
        }

        private void ThrowIfDisposed()
            => ObjectDisposedException.ThrowIf(this.disposed, this);
    }
}
