// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Assets.Persistence.LooseCooked.V1;

namespace Oxygen.Assets.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
public sealed class LooseCookedIndexTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public void WriteThenRead_ShouldRoundTrip()
    {
        var doc = CreateSampleDocument();

        using var ms = new MemoryStream();
        LooseCookedIndex.Write(ms, doc);
        ms.Position = 0;

        var read = LooseCookedIndex.Read(ms);

        _ = read.ContentVersion.Should().Be(doc.ContentVersion);
        _ = read.Assets.Should().HaveCount(2);
        _ = read.Files.Should().HaveCount(2);

        _ = read.Assets[0].DescriptorRelativePath.Should().Be(doc.Assets[0].DescriptorRelativePath);
        _ = read.Assets[0].VirtualPath.Should().Be(doc.Assets[0].VirtualPath);
        _ = read.Assets[0].AssetKey.Should().Be(doc.Assets[0].AssetKey);
        _ = read.Assets[0].DescriptorSha256.Span.ToArray().Should().Equal(doc.Assets[0].DescriptorSha256.Span.ToArray());

        _ = read.Files[0].Kind.Should().Be(doc.Files[0].Kind);
        _ = read.Files[0].RelativePath.Should().Be(doc.Files[0].RelativePath);
        _ = read.Files[0].Sha256.Span.ToArray().Should().Equal(doc.Files[0].Sha256.Span.ToArray());
    }

    [TestMethod]
    public void Write_ShouldBeDeterministicGivenSameInput()
    {
        var doc = CreateSampleDocument();

        byte[] a;
        byte[] b;

        using (var ms = new MemoryStream())
        {
            LooseCookedIndex.Write(ms, doc);
            a = ms.ToArray();
        }

        using (var ms = new MemoryStream())
        {
            LooseCookedIndex.Write(ms, doc);
            b = ms.ToArray();
        }

        _ = a.Should().Equal(b);
    }

    [TestMethod]
    public void Read_WithInvalidMagic_ShouldThrow()
    {
        using var ms = new MemoryStream(new byte[LooseCookedIndex.HeaderSize]);
        Action act = () => _ = LooseCookedIndex.Read(ms);
        _ = act.Should().Throw<InvalidDataException>();
    }

    [TestMethod]
    public void Read_WithUnknownFlagsBits_ShouldThrow()
    {
        var doc = CreateSampleDocument();

        byte[] bytes;
        using (var ms = new MemoryStream())
        {
            LooseCookedIndex.Write(ms, doc);
            bytes = ms.ToArray();
        }

        // Flags field starts at byte offset 12 in the header.
        bytes[12] = 0x00;
        bytes[13] = 0x00;
        bytes[14] = 0x00;
        bytes[15] = 0x80; // set a high unknown bit

        using var readStream = new MemoryStream(bytes, writable: false);
        Action act = () => _ = LooseCookedIndex.Read(readStream);
        _ = act.Should().Throw<InvalidDataException>();
    }

    [TestMethod]
    public void Read_WithStringOffsetOutsideTable_ShouldThrow()
    {
        var doc = CreateSampleDocument();

        byte[] bytes;
        using (var ms = new MemoryStream())
        {
            LooseCookedIndex.Write(ms, doc);
            bytes = ms.ToArray();
        }

        // Header layout:
        // - string_table_size at offset 24 (u64)
        // - asset_entries_offset at offset 32 (u64)
        var stringTableSize = BinaryPrimitives.ReadUInt64LittleEndian(bytes.AsSpan(24, 8));
        var assetEntriesOffset = BinaryPrimitives.ReadUInt64LittleEndian(bytes.AsSpan(32, 8));

        // Asset entry layout (v1 minimum):
        // - desc_rel_offset at +16 (u32)
        // Set it to an offset beyond the string table.
        var firstAssetDescOffsetField = checked((int)assetEntriesOffset) + 16;
        BinaryPrimitives.WriteUInt32LittleEndian(
            bytes.AsSpan(firstAssetDescOffsetField, 4),
            checked((uint)stringTableSize + 1));

        using var readStream = new MemoryStream(bytes, writable: false);
        Action act = () => _ = LooseCookedIndex.Read(readStream);
        _ = act.Should().Throw<InvalidDataException>();
    }

    private static Document CreateSampleDocument()
    {
        var shaA = LooseCookedIndex.ComputeSha256("descriptor-a"u8);
        var shaB = LooseCookedIndex.ComputeSha256("descriptor-b"u8);
        var shaF1 = LooseCookedIndex.ComputeSha256("file-1"u8);
        var shaF2 = LooseCookedIndex.ComputeSha256("file-2"u8);

        return new Document(
            ContentVersion: 7,
            Flags: IndexFeatures.HasVirtualPaths | IndexFeatures.HasFileRecords,
            Assets:
            [
                new AssetEntry(
                    AssetKey: new AssetKey(0x0102030405060708UL, 0x1112131415161718UL),
                    DescriptorRelativePath: "assets/Materials/Wood.mat",
                    VirtualPath: "/Content/Materials/Wood.mat",
                    AssetType: 2,
                    DescriptorSize: 123,
                    DescriptorSha256: shaA),
                new AssetEntry(
                    AssetKey: new AssetKey(0x2122232425262728UL, 0x3132333435363738UL),
                    DescriptorRelativePath: "assets/Geometry/Cube.geo",
                    VirtualPath: "/Engine/Geometry/Cube.geo",
                    AssetType: 1,
                    DescriptorSize: 456,
                    DescriptorSha256: shaB),
            ],
            Files:
            [
                new FileRecord(
                    Kind: FileKind.BuffersTable,
                    RelativePath: "resources/buffers.table",
                    Size: 1000,
                    Sha256: shaF1),
                new FileRecord(
                    Kind: FileKind.TexturesData,
                    RelativePath: "resources/textures.data",
                    Size: 2000,
                    Sha256: shaF2),
            ]);
    }
}
