// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using System.Reactive.Subjects;
using Oxygen.Editor.ContentPipeline.Discovery;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentBrowser.Infrastructure.Assets;

/// <summary>Projects the engine's current or last-known metadata into the composed project catalog.</summary>
public sealed partial class EngineBuiltinAssetCatalog : IAssetCatalog, IRefreshableAssetCatalog, IDisposable
{
    private readonly IBuiltinCatalogDiscovery discovery;
    private readonly Lock sync = new();
    private readonly Subject<AssetChange> changes = new();
    private Dictionary<Uri, AssetRecord> records = [];
    private GeneratedAssetCatalog catalog = new([]);
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="EngineBuiltinAssetCatalog"/> class.</summary>
    /// <param name="discovery">The workspace-owned native discovery source.</param>
    public EngineBuiltinAssetCatalog(IBuiltinCatalogDiscovery discovery)
    {
        this.discovery = discovery;
        this.discovery.Changed += this.OnDiscoveryChanged;
    }

    /// <inheritdoc />
    public IObservable<AssetChange> Changes => this.changes.AsObservable();

    /// <inheritdoc />
    public async Task<IReadOnlyList<AssetRecord>> QueryAsync(AssetQuery query, CancellationToken cancellationToken = default)
    {
        _ = await this.discovery.GetAsync(cancellationToken).ConfigureAwait(false);
        this.OnDiscoveryChanged(this, EventArgs.Empty);
        GeneratedAssetCatalog current;
        lock (this.sync)
        {
            ObjectDisposedException.ThrowIf(this.disposed, this);
            current = this.catalog;
        }

        return await current.QueryAsync(query, cancellationToken).ConfigureAwait(false);
    }

    /// <inheritdoc />
    public async Task RefreshAsync(CancellationToken cancellationToken = default)
    {
        _ = await this.discovery.RefreshAsync(cancellationToken).ConfigureAwait(false);
        this.OnDiscoveryChanged(this, EventArgs.Empty);
    }

    /// <inheritdoc />
    public void Dispose()
    {
        lock (this.sync)
        {
            if (this.disposed)
            {
                return;
            }

            this.disposed = true;
            this.discovery.Changed -= this.OnDiscoveryChanged;
            this.changes.Dispose();
        }
    }

    private void OnDiscoveryChanged(object? sender, EventArgs args)
    {
        var snapshot = this.discovery.Snapshot;
        var next = (snapshot.Catalog?.CreateCatalogRecords() ?? []).Select(record => record with
        {
            Generated = record.Generated! with { IsLastKnown = snapshot.IsLastKnown },
        }).ToDictionary(static record => record.Uri);
        lock (this.sync)
        {
            if (this.disposed || !ReferenceEquals(snapshot, this.discovery.Snapshot))
            {
                return;
            }

            var events = this.records.Keys.Where(uri => !next.ContainsKey(uri)).Select(static uri => new AssetChange(AssetChangeKind.Removed, uri))
                .Concat(next.Values.Where(record => !this.records.TryGetValue(record.Uri, out var prior) || prior != record)
                    .Select(record => new AssetChange(this.records.ContainsKey(record.Uri) ? AssetChangeKind.Updated : AssetChangeKind.Added, record.Uri))).ToArray();
            this.records = next;
            this.catalog = new(next.Values);
            foreach (var change in events)
            {
                this.changes.OnNext(change);
            }
        }
    }
}
