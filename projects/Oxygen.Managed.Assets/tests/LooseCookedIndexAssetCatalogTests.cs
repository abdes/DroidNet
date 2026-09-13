// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Catalog.LooseCooked;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V1;
using Testably.Abstractions.Testing;

namespace Oxygen.Managed.Assets.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
public sealed class LooseCookedIndexAssetCatalogTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task QueryAsync_ShouldEnumerateAssetsFromIndexVirtualPaths()
    {
        var fs = new MockFileSystem();
        _ = fs.Directory.CreateDirectory(@"C:\Cooked");

        var indexDoc = new Document(
            ContentVersion: 1,
            Flags: IndexFeatures.HasVirtualPaths,
            SourceGuid: Guid.CreateVersion7(),
            Assets:
            [
                new AssetEntry(
                    AssetKey: new AssetKey(1, 2),
                    DescriptorRelativePath: "assets/A.asset",
                    VirtualPath: "/Content/A.asset",
                    AssetType: 1,
                    DescriptorSize: 0,
                    DescriptorSha256: new byte[LooseCookedIndex.Sha256Size]),
                new AssetEntry(
                    AssetKey: new AssetKey(3, 4),
                    DescriptorRelativePath: "assets/B.asset",
                    VirtualPath: "/Engine/B.asset",
                    AssetType: 1,
                    DescriptorSize: 0,
                    DescriptorSha256: new byte[LooseCookedIndex.Sha256Size]),
            ],
            Files: []);

        var ms = new MemoryStream();
        await using (ms.ConfigureAwait(false))
        {
            LooseCookedIndex.Write(ms, indexDoc);
            await fs.File.WriteAllBytesAsync(@"C:\Cooked\container.index.bin", ms.ToArray(), this.TestContext.CancellationToken).ConfigureAwait(true);
        }

        var storage = new NativeStorageProvider(fs);
        using var catalog = new LooseCookedIndexAssetCatalog(storage, new LooseCookedIndexAssetCatalogOptions
        {
            CookedRootFolderPath = @"C:\Cooked",
        });

        var results = await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = results.Select(r => r.Uri).Should().Contain(new Uri("asset:///Content/A.asset"));
        _ = results.Select(r => r.Uri).Should().Contain(new Uri("asset:///Engine/B.asset"));
        var first = results.Single(record => record.Uri == new Uri("asset:///Content/A.asset"));
        _ = first.Cooked.Should().Be(new CookedAssetMetadata(@"C:\Cooked", "assets/A.asset", indexDoc.SourceGuid, new AssetKey(1, 2), 1, 0, new string('0', 64)) { VirtualPath = "/Content/A.asset" });
    }

    /// <summary>Reloaded records retain native types and content revisions without deriving them from a filename.</summary>
    /// <returns>The asynchronous indexed metadata regression.</returns>
    [TestMethod]
    public async Task RefreshPreservesIndexedTypesAndReplacesTheCompleteSnapshot()
    {
        var fs = new MockFileSystem();
        _ = fs.Directory.CreateDirectory(@"C:\Cooked");
        var entry = new AssetEntry(new AssetKey(1, 2), "payloads/7.bin", "/Content/Materials/Wood.png", AssetType: 1, DescriptorSize: 17, new byte[LooseCookedIndex.Sha256Size]);
        var document = new Document(1, IndexFeatures.HasVirtualPaths, Guid.CreateVersion7(), [entry], []);
        await WriteIndexAsync(document).ConfigureAwait(false);
        using var catalog = new LooseCookedIndexAssetCatalog(new NativeStorageProvider(fs), new LooseCookedIndexAssetCatalogOptions { CookedRootFolderPath = @"C:\Cooked" });
        var original = (await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false)).Single();
        _ = original.Cooked!.AssetType.Should().Be(1);
        _ = original.Cooked.DescriptorRelativePath.Should().Be("payloads/7.bin");
        await catalog.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = (await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false)).Single().Should().Be(original);
        var updated = entry with { DescriptorSha256 = Enumerable.Repeat((byte)1, 32).ToArray(), DescriptorSize = 18 };
        await WriteIndexAsync(document with { Assets = [updated] }).ConfigureAwait(false);
        await catalog.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var current = (await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false)).Single();
        _ = current.Cooked!.DescriptorSize.Should().Be(18);
        _ = current.Should().NotBe(original);
        _ = original.Cooked.DescriptorSize.Should().Be(17, "an already returned snapshot must remain unchanged");

        async Task WriteIndexAsync(Document value)
        {
            var stream = new MemoryStream();
            await using var streamLifetime = stream.ConfigureAwait(false);
            LooseCookedIndex.Write(stream, value);
            await fs.File.WriteAllBytesAsync(@"C:\Cooked\container.index.bin", stream.ToArray(), this.TestContext.CancellationToken).ConfigureAwait(false);
        }
    }

    /// <summary>Mount precedence applies between containers, not to malformed duplicate entries inside one index.</summary>
    /// <returns>The asynchronous duplicate-path regression.</returns>
    [TestMethod]
    public async Task RejectsRepeatedVirtualPathsWithinOneIndex()
    {
        var fs = new MockFileSystem();
        _ = fs.Directory.CreateDirectory(@"C:\Cooked");
        var entry = new AssetEntry(new AssetKey(1, 2), "payloads/7.bin", "/Content/Shape", AssetType: 2, DescriptorSize: 17, new byte[LooseCookedIndex.Sha256Size]);
        var document = new Document(1, IndexFeatures.HasVirtualPaths, Guid.CreateVersion7(), [entry, entry with { AssetKey = new AssetKey(3, 4) }], []);
        var stream = new MemoryStream();
        await using var streamLifetime = stream.ConfigureAwait(false);
        LooseCookedIndex.Write(stream, document);
        await fs.File.WriteAllBytesAsync(@"C:\Cooked\container.index.bin", stream.ToArray(), this.TestContext.CancellationToken).ConfigureAwait(false);
        using var catalog = new LooseCookedIndexAssetCatalog(new NativeStorageProvider(fs), new LooseCookedIndexAssetCatalogOptions { CookedRootFolderPath = @"C:\Cooked" });
        Func<Task> query = () => catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken);
        _ = await query.Should().ThrowAsync<InvalidDataException>().WithMessage("*duplicate virtual path*").ConfigureAwait(false);
    }
}
