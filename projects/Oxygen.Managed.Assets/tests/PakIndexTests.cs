// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Assets.Persistence.Pak;
using Oxygen.Testing;

namespace Oxygen.Managed.Assets.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
public sealed class PakIndexTests
{
    private const string CubePath = "/Engine/Generated/BasicShapes/Cube.ogeo";
    private const string MaterialPath = "/Engine/Generated/Materials/Default.omat";
    private static readonly string Digest = Convert.ToHexString(Enumerable.Range(0, 32).Select(static value => (byte)value).ToArray());

    [TestMethod]
    public void Read_ShouldListTheBrowseIndexAssets()
    {
        var document = CreateDocument();
        using var stream = Write(document);

        var read = PakIndex.Read(stream);

        _ = read.ContentVersion.Should().Be(document.ContentVersion);
        _ = read.SourceIdentity.Should().Be(document.SourceIdentity);
        _ = read.CatalogDigest.Should().Be(document.CatalogDigest);
        _ = read.Assets.Select(static asset => asset.VirtualPath).Should().Equal(CubePath, MaterialPath);
        _ = read.Assets[0].AssetKey.Should().Be(document.Assets[0].AssetKey);
        _ = read.Assets[0].AssetType.Should().Be(document.Assets[0].AssetType);
        _ = read.Assets[0].DescriptorOffset.Should().Be(PakIndex.HeaderSize);
        _ = read.Assets[0].DescriptorSize.Should().Be(document.Assets[0].DescriptorSize);
        _ = read.Assets[0].References.Should().Be(document.Assets[0].References);
        _ = read.Assets[1].DescriptorOffset.Should().Be(PakIndex.HeaderSize + document.Assets[0].DescriptorSize);
    }

    [TestMethod]
    public void Read_WithoutBrowseIndex_ShouldThrow()
    {
        using var stream = new MemoryStream();
        PakFixture.Write(stream, CreateDocument(), browseIndex: false);

        AssertRejected(stream);
    }

    [TestMethod]
    [DataRow(8)]
    [DataRow(10)]
    public void Read_WithAnotherFormatVersion_ShouldThrow(int version)
    {
        using var stream = Write(CreateDocument());
        BinaryPrimitives.WriteUInt16LittleEndian(stream.GetBuffer().AsSpan(8, 2), checked((ushort)version));

        AssertRejected(stream);
    }

    [TestMethod]
    public void Read_WithABrowseEntryMissingFromTheDirectory_ShouldThrow()
    {
        using var stream = new MemoryStream();
        PakFixture.Write(stream, CreateDocument(), browseOnly: [Asset("/Engine/Internal/Missing.ogeo", 3)]);

        AssertRejected(stream);
    }

    [TestMethod]
    public void Read_WithATexturePathNamingAnAsset_ShouldThrow()
    {
        using var stream = new MemoryStream();
        PakFixture.Write(stream, CreateDocument(), textures: [(1, CubePath)], textureTableCount: 2);

        AssertRejected(stream);
    }

    [TestMethod]
    [DataRow(0u)]
    [DataRow(2u)]
    public void Read_WithATextureOutsideTheTextureTable_ShouldThrow(uint textureIndex)
    {
        using var stream = new MemoryStream();
        PakFixture.Write(stream, CreateDocument(), textures: [(textureIndex, "/Engine/Generated/Textures/White.otex")], textureTableCount: 2);

        AssertRejected(stream);
    }

    [TestMethod]
    public void Read_WithTexturesInsideTheTextureTable_ShouldListOnlyAssets()
    {
        using var stream = new MemoryStream();
        PakFixture.Write(stream, CreateDocument(), textures: [(1, "/Engine/Generated/Textures/White.otex")], textureTableCount: 2);

        var read = PakIndex.Read(stream);

        _ = read.Assets.Should().HaveCount(2);
    }

    private static void AssertRejected(MemoryStream stream)
    {
        stream.Position = 0;
        Action read = () => _ = PakIndex.Read(stream);
        _ = read.Should().Throw<InvalidDataException>();
    }

    private static MemoryStream Write(PakDocument document)
    {
        var stream = new MemoryStream();
        PakFixture.Write(stream, document);
        stream.Position = 0;
        return stream;
    }

    private static PakDocument CreateDocument()
        => new(3, Guid.CreateVersion7(), Digest, [Asset(CubePath, 1), Asset(MaterialPath, 2)]);

    private static PakAssetEntry Asset(string virtualPath, byte seed)
        => new(new AssetKey(seed, seed * 10UL), seed, virtualPath, 0, 16U * seed);
}
