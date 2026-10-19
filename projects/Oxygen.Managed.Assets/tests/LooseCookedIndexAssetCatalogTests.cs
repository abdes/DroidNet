// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Security.Cryptography;
using AwesomeAssertions;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Catalog.LooseCooked;
using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
public sealed class LooseCookedIndexAssetCatalogTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task CapturedIndex_RemainsReadableWithoutItsFiles()
    {
        var path = Path.Combine(Path.GetTempPath(), "oxygen-catalog-snapshot-" + Guid.NewGuid().ToString("N"));
        var entry = new AssetEntry(new AssetKey(1, 2), "assets/material.bin", "/Content/Material.omat", 1, 0, SHA256.HashData([]))
        {
            References = new(42, 3, 5),
        };
        var document = new Document(1, IndexFeatures.HasVirtualPaths, Guid.CreateVersion7(), [entry], []);
        var catalog = new LooseCookedIndexAssetCatalog(document, FileSystemPath.From(path));

        var before = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        var after = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = after.Should().Equal(before);
        _ = after.Should().ContainSingle();
        _ = after.Single().Cooked!.SourceIdentity.Should().Be(document.SourceGuid);
        _ = after.Single().Cooked!.References.Should().Be(entry.References);
        _ = Directory.Exists(path).Should().BeFalse();
    }

    /// <summary>Records carry the indexed types, paths and revisions without deriving them from a filename.</summary>
    /// <returns>The asynchronous indexed metadata check.</returns>
    [TestMethod]
    public async Task QueryAsync_ShouldEnumerateAssetsFromIndexVirtualPaths()
    {
        var root = FileSystemPath.From(@"C:\Cooked");
        var document = new Document(
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
                    DescriptorRelativePath: "payloads/7.bin",
                    VirtualPath: "/Engine/Materials/Wood.png",
                    AssetType: 2,
                    DescriptorSize: 17,
                    DescriptorSha256: SHA256.HashData(new byte[17])),
            ],
            Files: []);
        var catalog = new LooseCookedIndexAssetCatalog(document, root);

        var results = await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);

        var first = results.Single(record => record.Identity == AssetPath.Parse("/Content/A.asset"));
        _ = first.Cooked.Should().Be(new CookedAssetMetadata(root, "assets/A.asset", document.SourceGuid, new AssetKey(1, 2), 1, 0, Convert.ToHexString(SHA256.HashData([]))) { VirtualPath = AssetPath.Parse("/Content/A.asset") });
        var second = results.Single(record => record.Identity == AssetPath.Parse("/Engine/Materials/Wood.png")).Cooked!;
        _ = second.AssetType.Should().Be(2);
        _ = second.DescriptorRelativePath.Should().Be("payloads/7.bin");
        _ = second.DescriptorSize.Should().Be(17);
    }

    /// <summary>
    /// A texture descriptor is listed by its engine key with the facts its file states; one whose file is missing is
    /// listed without them.
    /// </summary>
    /// <returns>The asynchronous texture listing check.</returns>
    [TestMethod]
    public async Task QueryAsync_ShouldListTextureDescriptorsWithTheirFacts()
    {
        var root = Directory.CreateTempSubdirectory("oxygen-catalog-textures-");
        try
        {
            _ = Directory.CreateDirectory(Path.Combine(root.FullName, "Textures"));
            var cube = new TextureResourceRecord(TextureType: 5, Format: 39, Width: 16, Height: 16, ArrayLayers: 6);
            var sidecar = new byte[TextureDescriptorFile.Size];
            "OTEX"u8.CopyTo(sidecar);
            sidecar[4] = 1;
            sidecar[8] = 1;
            Oxygen.Testing.PakFixture.Encode(cube).CopyTo(sidecar, 12);
            await File.WriteAllBytesAsync(Path.Combine(root.FullName, "Textures", "Sky.otex"), sidecar, this.TestContext.CancellationToken).ConfigureAwait(false);
            var document = new Document(1, IndexFeatures.HasVirtualPaths, Guid.CreateVersion7(), [],
            [
                new FileRecord(FileKind.TextureDescriptor, "Textures/Sky.otex", (ulong)sidecar.Length, SHA256.HashData(sidecar), "/Content/Textures/Sky.otex"),
                new FileRecord(FileKind.TextureDescriptor, "Textures/Gone.otex", (ulong)sidecar.Length, SHA256.HashData(sidecar), "/Content/Textures/Gone.otex"),
            ]);
            var catalog = new LooseCookedIndexAssetCatalog(document, FileSystemPath.From(root.FullName));

            var records = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);

            var sky = records.Single(record => record.Identity == AssetPath.Parse("/Content/Textures/Sky.otex")).Cooked!;
            _ = sky.AssetKey.Should().Be(AssetKey.FromVirtualPath("/Content/Textures/Sky.otex"));
            _ = sky.AssetType.Should().Be((byte)AssetType.Unknown);
            _ = sky.DescriptorRelativePath.Should().Be("Textures/Sky.otex");
            _ = sky.Texture.Should().Be(cube);
            _ = records.Single(record => record.Identity == AssetPath.Parse("/Content/Textures/Gone.otex")).Cooked!.Texture.Should().BeNull();
        }
        finally
        {
            root.Delete(recursive: true);
        }
    }

    /// <summary>Mount precedence applies between containers, not to malformed duplicate entries inside one index.</summary>
    /// <returns>The asynchronous duplicate-path regression.</returns>
    [TestMethod]
    public async Task RejectsRepeatedVirtualPathsWithinOneIndex()
    {
        var entry = new AssetEntry(new AssetKey(1, 2), "payloads/7.bin", "/Content/Shape", AssetType: 2, DescriptorSize: 17, SHA256.HashData(new byte[17]));
        var document = new Document(1, IndexFeatures.HasVirtualPaths, Guid.CreateVersion7(), [entry, entry with { AssetKey = new AssetKey(3, 4), DescriptorRelativePath = "payloads/8.bin" }], []);
        var catalog = new LooseCookedIndexAssetCatalog(document, FileSystemPath.From(@"C:\Cooked"));
        Func<Task> query = () => catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken);
        _ = await query.Should().ThrowAsync<InvalidDataException>().ConfigureAwait(false);
    }
}
