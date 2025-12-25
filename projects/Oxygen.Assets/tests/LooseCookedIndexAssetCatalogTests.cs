// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Assets.Catalog;
using Oxygen.Assets.Catalog.LooseCooked;
using Oxygen.Assets.Persistence.LooseCooked.V1;
using Oxygen.Storage.Native;
using Testably.Abstractions.Testing;

namespace Oxygen.Assets.Tests;

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

        using (var ms = new MemoryStream())
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

        results.Select(r => r.Uri).Should().Contain(new Uri("asset:///Content/A.asset"));
        results.Select(r => r.Uri).Should().Contain(new Uri("asset:///Engine/B.asset"));
    }
}
