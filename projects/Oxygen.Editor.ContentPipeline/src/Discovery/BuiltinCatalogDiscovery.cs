// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Config;
using DroidNet.Storage;
using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline.Discovery;

/// <summary>Retains engine-owned metadata for offline authoring without supplying cached recipes to cooking.</summary>
public sealed partial class BuiltinCatalogDiscovery : IBuiltinCatalogDiscovery, IDisposable
{
    private readonly IBuiltinGeometryCatalogProvider nativeCatalog;
    private readonly IAtomicFileStore files;
    private readonly INativeCompatibilityService compatibility;
    private readonly ILogger<BuiltinCatalogDiscovery> logger;
    private readonly string cacheRoot;
    private readonly string cachePath;
    private readonly string queryRoot;
    private readonly Lock sync = new();
    private readonly CancellationTokenSource lifetime = new();
    private BuiltinCatalogSnapshot snapshot = new(Catalog: null, IsLastKnown: false, Notice: null);
    private Task<BuiltinCatalogSnapshot>? refresh;
    private string? producer;
    private bool initialized;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="BuiltinCatalogDiscovery"/> class.</summary>
    /// <param name="nativeCatalog">The strict native catalog provider also used by cooking.</param>
    /// <param name="files">The atomic cache reader and writer.</param>
    /// <param name="paths">The editor's application-state location.</param>
    /// <param name="logger">Discovery and cache diagnostics.</param>
    /// <param name="compatibility">The cooking SDK identity check.</param>
    public BuiltinCatalogDiscovery(IBuiltinGeometryCatalogProvider nativeCatalog, IAtomicFileStore files, IPathFinder paths, ILogger<BuiltinCatalogDiscovery> logger, INativeCompatibilityService? compatibility = null)
    {
        this.nativeCatalog = nativeCatalog;
        this.files = files;
        this.logger = logger;
        this.compatibility = compatibility ?? EditorNativeCompatibilityService.ForCooking();
        this.cacheRoot = Path.Combine(paths.LocalAppState, "cache", "builtins", EditorNativeCompatibilityService.CurrentConfiguration);
        this.cachePath = Path.Combine(this.cacheRoot, "catalog.json");
        this.queryRoot = Path.Combine(paths.Temp, "Oxygen", "BuiltinCatalog", EditorNativeCompatibilityService.CurrentConfiguration);
    }

    /// <inheritdoc />
    public event EventHandler? Changed;

    /// <inheritdoc />
    public BuiltinCatalogSnapshot Snapshot
    {
        get
        {
            lock (this.sync)
            {
                return this.snapshot;
            }
        }
    }

    /// <inheritdoc />
    public Task<BuiltinCatalogSnapshot> GetAsync(CancellationToken cancellationToken = default)
    {
        lock (this.sync)
        {
            ObjectDisposedException.ThrowIf(this.disposed, this);
            cancellationToken.ThrowIfCancellationRequested();
            return this.initialized ? Task.FromResult(this.snapshot) : this.StartRefresh().WaitAsync(cancellationToken);
        }
    }

    /// <inheritdoc />
    public Task<BuiltinCatalogSnapshot> RefreshAsync(CancellationToken cancellationToken = default)
    {
        lock (this.sync)
        {
            ObjectDisposedException.ThrowIf(this.disposed, this);
            cancellationToken.ThrowIfCancellationRequested();
            return this.StartRefresh().WaitAsync(cancellationToken);
        }
    }

    /// <inheritdoc />
    public void Dispose()
    {
        Task<BuiltinCatalogSnapshot>? pending;
        lock (this.sync)
        {
            if (this.disposed)
            {
                return;
            }

            this.disposed = true;
            this.lifetime.Cancel();
            pending = this.refresh;
        }

        if (pending?.IsCompleted != false)
        {
            this.lifetime.Dispose();
        }
        else
        {
            _ = this.DisposeAfterRefreshAsync(pending);
        }
    }

    private Task<BuiltinCatalogSnapshot> StartRefresh()
    {
        if (this.refresh?.IsCompleted != false)
        {
            this.refresh = Task.Run(() => this.RefreshCoreAsync(this.lifetime.Token), CancellationToken.None);
        }

        return this.refresh;
    }

    private async Task<BuiltinCatalogSnapshot> RefreshCoreAsync(CancellationToken cancellationToken)
    {
        var previous = this.Snapshot;
        BuiltinCatalogSnapshot result;
        try
        {
            var native = await this.compatibility.VerifyAsync(Guid.NewGuid(), cancellationToken).ConfigureAwait(false);
            if (!native.Succeeded)
            {
                if (native.Artifacts is { } rejected)
                {
                    await rejected.DisposeAsync().ConfigureAwait(false);
                }

                throw new NativeCompatibilityException(native.Diagnostics);
            }

            var artifacts = native.Artifacts!;
            await using var artifactLifetime = artifacts.ConfigureAwait(false);
            cancellationToken.ThrowIfCancellationRequested();
            if (previous.IsCurrent && string.Equals(this.producer, artifacts.Fingerprint, StringComparison.Ordinal))
            {
                return previous;
            }

            Directory.CreateDirectory(this.queryRoot);
            var catalog = await this.QueryNativeAsync(artifacts, cancellationToken).ConfigureAwait(false);
            Validate(catalog);
            await this.WriteCacheAsync(catalog, cancellationToken).ConfigureAwait(false);
            this.producer = artifacts.Fingerprint;
            result = new(catalog, IsLastKnown: false, Notice: null);
        }
        catch (Exception exception) when (IsCatalogFailure(exception))
        {
            this.LogSdkUnavailable(exception);
            var cached = previous.Catalog ?? await this.ReadCacheAsync(cancellationToken).ConfigureAwait(false);
            result = cached is null
                ? new(Catalog: null, IsLastKnown: false, "Engine catalog unavailable. No last-known catalog is available; repair the SDK and refresh.")
                : new(cached, IsLastKnown: true, "Using the last-known engine catalog. Preview unavailable; repair the SDK and refresh.");
        }

        cancellationToken.ThrowIfCancellationRequested();
        lock (this.sync)
        {
            ObjectDisposedException.ThrowIf(this.disposed, this);
            this.snapshot = result;
            this.initialized = true;
        }

        if (result != previous)
        {
            this.PublishChanged();
        }

        return result;
    }
}
