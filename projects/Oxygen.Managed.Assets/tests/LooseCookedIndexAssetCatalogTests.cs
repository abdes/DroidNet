// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Security.Cryptography;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Catalog.LooseCooked;
using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Managed.Assets.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
public sealed class LooseCookedIndexAssetCatalogTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task CapturedIndex_RemainsReadableWithoutItsFilesAndDoesNotReload()
    {
        var path = Path.Combine(Path.GetTempPath(), "oxygen-catalog-snapshot-" + Guid.NewGuid().ToString("N"));
        var entry = new AssetEntry(new AssetKey(1, 2), "assets/material.bin", "/Content/Material.omat", 1, 0, SHA256.HashData([]))
        {
            References = new(42, 3, 5),
        };
        var document = new Document(1, IndexFeatures.HasVirtualPaths, Guid.CreateVersion7(), [entry], []);
        using var catalog = new LooseCookedIndexAssetCatalog(document, FileSystemPath.From(path));

        var before = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        await catalog.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var after = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = after.Should().Equal(before);
        _ = after.Should().ContainSingle();
        _ = after.Single().Cooked!.SourceIdentity.Should().Be(document.SourceGuid);
        _ = after.Single().Cooked!.References.Should().Be(entry.References);
        _ = Directory.Exists(path).Should().BeFalse();
    }

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
                    DescriptorSha256: SHA256.HashData([])),
                new AssetEntry(
                    AssetKey: new AssetKey(3, 4),
                    DescriptorRelativePath: "assets/B.asset",
                    VirtualPath: "/Engine/B.asset",
                    AssetType: 1,
                    DescriptorSize: 0,
                    DescriptorSha256: SHA256.HashData([])),
            ],
            Files: []);

        var ms = new MemoryStream();
        await using (ms.ConfigureAwait(false))
        {
            Oxygen.Testing.LooseCookedIndexFixture.Write(ms, indexDoc);
            await fs.File.WriteAllBytesAsync(@"C:\Cooked\container.index.bin", ms.ToArray(), this.TestContext.CancellationToken).ConfigureAwait(true);
        }

        var storage = new NativeStorageProvider(fs);
        using var catalog = new LooseCookedIndexAssetCatalog(storage, new LooseCookedIndexAssetCatalogOptions
        {
            CookedRootFolderPath = FileSystemPath.From(@"C:\Cooked"),
        });

        var results = await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = results.Select(r => r.Identity).Should().Contain(AssetPath.Parse("/Content/A.asset"));
        _ = results.Select(r => r.Identity).Should().Contain(AssetPath.Parse("/Engine/B.asset"));
        var first = results.Single(record => record.Identity == AssetPath.Parse("/Content/A.asset"));
        _ = first.Cooked.Should().Be(new CookedAssetMetadata(FileSystemPath.From(@"C:\Cooked"), "assets/A.asset", indexDoc.SourceGuid, new AssetKey(1, 2), 1, 0, Convert.ToHexString(SHA256.HashData([]))) { VirtualPath = AssetPath.Parse("/Content/A.asset") });
    }

    /// <summary>Reloaded records retain native types and content revisions without deriving them from a filename.</summary>
    /// <returns>The asynchronous indexed metadata regression.</returns>
    [TestMethod]
    public async Task RefreshPreservesIndexedTypesAndReplacesTheCompleteSnapshot()
    {
        var fs = new MockFileSystem();
        _ = fs.Directory.CreateDirectory(@"C:\Cooked");
        var entry = new AssetEntry(new AssetKey(1, 2), "payloads/7.bin", "/Content/Materials/Wood.png", AssetType: 1, DescriptorSize: 17, SHA256.HashData(new byte[17]))
        {
            References = new(0, 1, 2),
        };
        var document = new Document(1, IndexFeatures.HasVirtualPaths, Guid.CreateVersion7(), [entry], []);
        await WriteIndexAsync(document).ConfigureAwait(false);
        using var catalog = new LooseCookedIndexAssetCatalog(new NativeStorageProvider(fs), new LooseCookedIndexAssetCatalogOptions { CookedRootFolderPath = FileSystemPath.From(@"C:\Cooked") });
        var original = (await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false)).Single();
        _ = original.Cooked!.AssetType.Should().Be(1);
        _ = original.Cooked.DescriptorRelativePath.Should().Be("payloads/7.bin");
        _ = original.Cooked.References.ResourceCount.Should().Be(1);
        _ = original.Cooked.References.KeyCount.Should().Be(2);
        _ = original.Cooked.References.Offset.Should().BeGreaterThan(0);
        await catalog.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = (await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false)).Single().Should().Be(original);
        var updated = entry with
        {
            DescriptorSha256 = Enumerable.Repeat((byte)1, 32).ToArray(),
            DescriptorSize = 18,
            References = new(0, 2, 3),
        };
        await WriteIndexAsync(document with { Assets = [updated] }).ConfigureAwait(false);
        await catalog.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var current = (await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false)).Single();
        _ = current.Cooked!.DescriptorSize.Should().Be(18);
        _ = current.Cooked.References.ResourceCount.Should().Be(2);
        _ = current.Cooked.References.KeyCount.Should().Be(3);
        _ = current.Cooked.References.Offset.Should().Be(original.Cooked.References.Offset);
        _ = current.Should().NotBe(original);
        _ = original.Cooked.DescriptorSize.Should().Be(17, "an already returned snapshot must remain unchanged");
        _ = original.Cooked.References.ResourceCount.Should().Be(1, "an already returned snapshot must retain its reference metadata");

        async Task WriteIndexAsync(Document value)
        {
            var stream = new MemoryStream();
            await using var streamLifetime = stream.ConfigureAwait(false);
            Oxygen.Testing.LooseCookedIndexFixture.Write(stream, value);
            await fs.File.WriteAllBytesAsync(@"C:\Cooked\container.index.bin", stream.ToArray(), this.TestContext.CancellationToken).ConfigureAwait(false);
        }
    }

    /// <summary>
    /// A texture descriptor is listed by its engine key with the facts its file states; one whose file is missing is
    /// listed without them.
    /// </summary>
    /// <returns>The asynchronous texture listing check.</returns>
    [TestMethod]
    public async Task QueryAsync_ShouldListTextureDescriptorsWithTheirFacts()
    {
        var fs = new MockFileSystem();
        _ = fs.Directory.CreateDirectory(@"C:\Cooked\Textures");
        var cube = new TextureResourceRecord(TextureType: 5, Format: 39, Width: 16, Height: 16, ArrayLayers: 6);
        var sidecar = new byte[TextureDescriptorFile.Size];
        "OTEX"u8.CopyTo(sidecar);
        sidecar[4] = 1;
        sidecar[8] = 1;
        Oxygen.Testing.PakFixture.Encode(cube).CopyTo(sidecar, 12);
        await fs.File.WriteAllBytesAsync(@"C:\Cooked\Textures\Sky.otex", sidecar, this.TestContext.CancellationToken).ConfigureAwait(false);
        var document = new Document(1, IndexFeatures.HasVirtualPaths, Guid.CreateVersion7(), [],
        [
            new FileRecord(FileKind.TextureDescriptor, "Textures/Sky.otex", (ulong)sidecar.Length, SHA256.HashData(sidecar), "/Content/Textures/Sky.otex"),
            new FileRecord(FileKind.TextureDescriptor, "Textures/Gone.otex", (ulong)sidecar.Length, SHA256.HashData(sidecar), "/Content/Textures/Gone.otex"),
        ]);
        var stream = new MemoryStream();
        await using var streamLifetime = stream.ConfigureAwait(false);
        Oxygen.Testing.LooseCookedIndexFixture.Write(stream, document);
        await fs.File.WriteAllBytesAsync(@"C:\Cooked\container.index.bin", stream.ToArray(), this.TestContext.CancellationToken).ConfigureAwait(false);
        using var catalog = new LooseCookedIndexAssetCatalog(new NativeStorageProvider(fs), new LooseCookedIndexAssetCatalogOptions { CookedRootFolderPath = FileSystemPath.From(@"C:\Cooked") });

        var records = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);

        var sky = records.Single(record => record.Identity == AssetPath.Parse("/Content/Textures/Sky.otex")).Cooked!;
        _ = sky.AssetKey.Should().Be(AssetKey.FromVirtualPath("/Content/Textures/Sky.otex"));
        _ = sky.AssetType.Should().Be((byte)AssetType.Unknown);
        _ = sky.DescriptorRelativePath.Should().Be("Textures/Sky.otex");
        _ = sky.Texture.Should().Be(cube);
        _ = records.Single(record => record.Identity == AssetPath.Parse("/Content/Textures/Gone.otex")).Cooked!.Texture.Should().BeNull();
    }

    /// <summary>Mount precedence applies between containers, not to malformed duplicate entries inside one index.</summary>
    /// <returns>The asynchronous duplicate-path regression.</returns>
    [TestMethod]
    public async Task RejectsRepeatedVirtualPathsWithinOneIndex()
    {
        var fs = new MockFileSystem();
        _ = fs.Directory.CreateDirectory(@"C:\Cooked");
        var entry = new AssetEntry(new AssetKey(1, 2), "payloads/7.bin", "/Content/Shape", AssetType: 2, DescriptorSize: 17, SHA256.HashData(new byte[17]));
        var document = new Document(1, IndexFeatures.HasVirtualPaths, Guid.CreateVersion7(), [entry, entry with { AssetKey = new AssetKey(3, 4), DescriptorRelativePath = "payloads/8.bin" }], []);
        var stream = new MemoryStream();
        await using var streamLifetime = stream.ConfigureAwait(false);
        Oxygen.Testing.LooseCookedIndexFixture.Write(stream, document);
        await fs.File.WriteAllBytesAsync(@"C:\Cooked\container.index.bin", stream.ToArray(), this.TestContext.CancellationToken).ConfigureAwait(false);
        using var catalog = new LooseCookedIndexAssetCatalog(new NativeStorageProvider(fs), new LooseCookedIndexAssetCatalogOptions { CookedRootFolderPath = FileSystemPath.From(@"C:\Cooked") });
        Func<Task> query = () => catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken);
        _ = await query.Should().ThrowAsync<InvalidDataException>().ConfigureAwait(false);
    }
}
