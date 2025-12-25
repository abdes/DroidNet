// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reactive.Subjects;
using AwesomeAssertions;
using Oxygen.Assets.Catalog;

namespace Oxygen.Assets.Tests;

/// <summary>
/// Unit tests for the <see cref="CompositeAssetCatalog"/> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
public sealed class CompositeAssetCatalogTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task QueryAsync_ShouldMergeAndDeduplicateByUri()
    {
        // Arrange
        var generated = new GeneratedAssetCatalog();

        var extra = new TestCatalog(
            new AssetRecord(new Uri("asset:///Engine/Generated/BasicShapes/Cube")), // duplicate
            new AssetRecord(new Uri("asset:///Content/Textures/Wood01")));

        var composite = new CompositeAssetCatalog(generated, extra);

        // Act
        var results = await composite.QueryAsync(new AssetQuery(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = results.Select(r => r.Uri)
            .Should().Contain(new Uri("asset:///Content/Textures/Wood01"));

        _ = results.Count(r => r.Uri.ToString().Contains("/BasicShapes/Cube", StringComparison.Ordinal))
            .Should().Be(1, "duplicate URIs from multiple providers must be de-duplicated");
    }

    [TestMethod]
    public void Changes_ShouldMergeProviderStreams()
    {
        // Arrange
        using var a = new TestCatalog();
        using var b = new TestCatalog();
        var composite = new CompositeAssetCatalog(a, b);

        var received = new List<AssetChange>();
        using var subscription = composite.Changes.Subscribe(received.Add);

        var change1 = new AssetChange(AssetChangeKind.Added, new Uri("asset:///Content/A"));
        var change2 = new AssetChange(AssetChangeKind.Removed, new Uri("asset:///Engine/B"));

        // Act
        a.Emit(change1);
        b.Emit(change2);

        // Assert
        _ = received.Should().Contain(change1);
        _ = received.Should().Contain(change2);
    }

    private sealed class TestCatalog(params AssetRecord[] records) : IAssetCatalog, IDisposable
    {
        private readonly Subject<AssetChange> subject = new();
        private readonly IReadOnlyList<AssetRecord> records = records;

        public IObservable<AssetChange> Changes => this.subject;

        public Task<IReadOnlyList<AssetRecord>> QueryAsync(AssetQuery query, CancellationToken cancellationToken = default)
            => Task.FromResult(this.records);

        public void Emit(AssetChange change) => this.subject.OnNext(change);

        public void Dispose()
        {
            this.subject.OnCompleted();
            this.subject.Dispose();
        }
    }
}
