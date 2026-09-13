// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Discovery;

namespace Oxygen.Testing;

/// <summary>Projects the captured native catalog into isolated editor presentation tests.</summary>
internal sealed class BuiltinCatalogDiscoveryFixture : IBuiltinCatalogDiscovery
{
    /// <inheritdoc />
    public event EventHandler? Changed;

    /// <inheritdoc />
    public BuiltinCatalogSnapshot Snapshot { get; private set; } = new(
        BuiltinGeometryCatalog.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "Fixtures", "BuiltinGeometryCatalog.json"))),
        IsLastKnown: false,
        Notice: null);

    /// <summary>Gets or sets the snapshot returned by the next simulated SDK retry.</summary>
    public BuiltinCatalogSnapshot? RefreshResult { get; set; }

    /// <inheritdoc />
    public Task<BuiltinCatalogSnapshot> GetAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return Task.FromResult(this.Snapshot);
    }

    /// <inheritdoc />
    public Task<BuiltinCatalogSnapshot> RefreshAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (this.RefreshResult is { } restored)
        {
            this.RefreshResult = null;
            this.SetSnapshot(restored);
        }

        return this.GetAsync(cancellationToken);
    }

    /// <summary>Publishes a simulated catalog or availability transition.</summary>
    /// <param name="snapshot">The next immutable discovery state.</param>
    public void SetSnapshot(BuiltinCatalogSnapshot snapshot)
    {
        this.Snapshot = snapshot;
        this.Changed?.Invoke(this, EventArgs.Empty);
    }
}
