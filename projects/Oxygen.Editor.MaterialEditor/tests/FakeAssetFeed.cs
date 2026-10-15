// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using Oxygen.Editor.Assets;

namespace Oxygen.Editor.MaterialEditor.Tests;

/// <summary>Stands in for the asset feed: a test publishes the entries the material editor would see.</summary>
internal sealed class FakeAssetFeed : IAssetFeed
{
    private readonly ReplaySubject<IReadOnlyList<AssetEntry>> items = new(bufferSize: 1);
    private IReadOnlyList<AssetEntry> latest = [];

    /// <inheritdoc/>
    public IObservable<IReadOnlyList<AssetEntry>> Items => this.items;

    /// <summary>Publishes a new set of entries, as the feed does after any change.</summary>
    /// <param name="entries">The entries now known.</param>
    public void Publish(IReadOnlyList<AssetEntry> entries)
    {
        this.latest = entries;
        this.items.OnNext(entries);
    }

    /// <inheritdoc/>
    public Task RefreshAsync(CancellationToken cancellationToken = default) => Task.CompletedTask;

    /// <inheritdoc/>
    public Task<AssetEntry?> ResolveAsync(Uri uri, CancellationToken cancellationToken = default)
        => Task.FromResult(this.latest.FirstOrDefault(entry => entry.IdentityUri == uri));
}
