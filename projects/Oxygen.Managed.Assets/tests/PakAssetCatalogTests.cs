// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Catalog.Pak;
using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Assets.Persistence.Pak;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
public sealed class PakAssetCatalogTests
{
    private static readonly FileSystemPath PakPath = FileSystemPath.From(@"C:\Sdk\share\oxygen\engine.pak");

    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task QueryAsync_ShouldListEachAssetInThePak()
    {
        var cube = new PakAssetEntry(new AssetKey(1, 2), 1, "/Engine/Generated/BasicShapes/Cube.ogeo", 256, 32) { References = new(512, 1, 0) };
        var document = new PakDocument(3, Guid.CreateVersion7(), new string('0', 64), [cube]);
        var catalog = new PakAssetCatalog(document, PakPath);

        var records = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);

        var record = records.Should().ContainSingle().Subject;
        _ = record.Identity.Should().Be(AssetPath.Parse(cube.VirtualPath));
        _ = record.Cooked.Should().Be(new CookedAssetMetadata(PakPath, DescriptorRelativePath: null, document.SourceIdentity, cube.AssetKey, cube.AssetType, cube.DescriptorSize, DescriptorSha256: null)
        {
            VirtualPath = record.Identity,
            References = cube.References,
        });
    }

    [TestMethod]
    public async Task QueryAsync_ShouldListEachTextureByItsEngineKey()
    {
        const string sky = "/Engine/Generated/Textures/Sky.otex";
        var texture = new TextureResourceRecord(TextureType: 5, Format: 39, Width: 16, Height: 16, ArrayLayers: 6);
        var document = new PakDocument(3, Guid.CreateVersion7(), new string('0', 64), []) { Textures = [new(sky, 1, texture)] };
        var catalog = new PakAssetCatalog(document, PakPath);

        var records = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);

        var record = records.Should().ContainSingle().Subject;
        _ = record.Identity.Should().Be(AssetPath.Parse(sky));
        _ = record.Cooked.Should().Be(new CookedAssetMetadata(PakPath, DescriptorRelativePath: null, document.SourceIdentity, AssetKey.FromVirtualPath(sky), (byte)AssetType.Unknown, TextureResourceRecord.Size, DescriptorSha256: null)
        {
            VirtualPath = record.Identity,
            Texture = texture,
        });
    }

    [TestMethod]
    public void Constructor_WithAMountPath_ShouldThrow()
    {
        var document = new PakDocument(3, Guid.CreateVersion7(), new string('0', 64), [new PakAssetEntry(new AssetKey(1, 2), 1, "/Engine", 256, 32)]);

        Action create = () => _ = new PakAssetCatalog(document, PakPath);

        _ = create.Should().Throw<InvalidDataException>();
    }
}
