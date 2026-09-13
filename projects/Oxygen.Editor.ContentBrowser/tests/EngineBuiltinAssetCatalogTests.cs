// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Discovery;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentBrowser.Tests;

/// <summary>Checks native discovery metadata and availability changes in the composed catalog.</summary>
[TestClass]
public sealed class EngineBuiltinAssetCatalogTests
{
    /// <summary>Gets or sets test cancellation.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Already-loaded native catalogs initialize adapters and retain identities across availability changes.</summary>
    /// <returns>The asynchronous catalog projection regression.</returns>
    [TestMethod]
    public async Task NativeCatalogAndLastKnownStateShareTheSameIdentities()
    {
        var native = BuiltinGeometryCatalog.Parse(await File.ReadAllTextAsync(Path.Combine(AppContext.BaseDirectory, "Fixtures", "BuiltinGeometryCatalog.json"), this.TestContext.CancellationToken).ConfigureAwait(false));
        var snapshot = new BuiltinCatalogSnapshot(native, IsLastKnown: false, Notice: null);
        var discovery = new Mock<IBuiltinCatalogDiscovery>();
        _ = discovery.SetupGet(value => value.Snapshot).Returns(() => snapshot);
        _ = discovery.Setup(value => value.GetAsync(It.IsAny<CancellationToken>())).Returns(() => Task.FromResult(snapshot));
        using var catalog = new EngineBuiltinAssetCatalog(discovery.Object);
        var initial = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = initial.Should().HaveCount(12);
        var changes = new List<AssetChange>();
        using var subscription = catalog.Changes.Subscribe(changes.Add);
        snapshot = new(native, IsLastKnown: true, "Preview unavailable");
        discovery.Raise(value => value.Changed += null, EventArgs.Empty);
        var cached = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = cached.Should().Equal(initial, (current, previous) => current.Uri == previous.Uri);
        _ = cached.Should().OnlyContain(record => record.Generated!.IsLastKnown);
        _ = changes.Should().HaveCount(12).And.OnlyContain(change => change.Kind == AssetChangeKind.Updated);
        _ = cached.Single(record => string.Equals(record.Name, "GeodesicSphere", StringComparison.Ordinal)).Generated!.CanonicalName.Should().Be("IcoSphere");
        changes.Clear();
        snapshot = new(Catalog: null, IsLastKnown: false, "No catalog available");
        discovery.Raise(value => value.Changed += null, EventArgs.Empty);
        _ = (await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeEmpty();
        _ = changes.Should().HaveCount(12).And.OnlyContain(change => change.Kind == AssetChangeKind.Removed);
    }
}
