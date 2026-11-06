// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Assets.Persistence.LooseCooked;

namespace Oxygen.Managed.Assets.Tests;

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
        Oxygen.Managed.Assets.Testing.LooseCookedIndexFixture.Write(ms, doc);
        ms.Position = 0;

        var read = LooseCookedIndex.Read(ms);

        _ = read.ContentVersion.Should().Be(doc.ContentVersion);
        _ = read.SourceGuid.Should().Be(doc.SourceGuid);
        _ = read.Assets.Should().HaveCount(2);
        _ = read.Files.Should().HaveCount(2);

        _ = read.Assets[0].DescriptorRelativePath.Should().Be(doc.Assets[0].DescriptorRelativePath);
        _ = read.Assets[0].VirtualPath.Should().Be(doc.Assets[0].VirtualPath);
        _ = read.Assets[0].AssetKey.Should().Be(doc.Assets[0].AssetKey);
        _ = read.Assets[0].SourcePath.Should().Be("/Content/Materials/Wood.mat.json", "each entry records the source that produced it");
        _ = read.Assets[0].DescriptorSha256.Span.ToArray().Should().Equal(doc.Assets[0].DescriptorSha256.Span.ToArray());
        _ = read.Assets[0].References.ResourceCount.Should().Be(1);
        _ = read.Assets[0].References.KeyCount.Should().Be(1);
        var encoded = ms.ToArray();
        var stringTableSize = BinaryPrimitives.ReadUInt64LittleEndian(encoded.AsSpan(40, 8));
        var expectedReferenceOffset = checked((ulong)LooseCookedIndex.HeaderSize + stringTableSize
            + ((ulong)doc.Assets.Count * LooseCookedIndex.AssetEntrySize)
            + ((ulong)doc.Files.Count * LooseCookedIndex.FileRecordSize));
        _ = read.Assets[0].References.Offset.Should().Be(expectedReferenceOffset);

        _ = read.Files[0].Kind.Should().Be(doc.Files[0].Kind);
        _ = read.Files[0].RelativePath.Should().Be(doc.Files[0].RelativePath);
        _ = read.Files[0].Size.Should().Be(doc.Files[0].Size);
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
            Oxygen.Managed.Assets.Testing.LooseCookedIndexFixture.Write(ms, doc);
            a = ms.ToArray();
        }

        using (var ms = new MemoryStream())
        {
            Oxygen.Managed.Assets.Testing.LooseCookedIndexFixture.Write(ms, doc);
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
    public void ReadFormatVersion_ShouldReadTheDeclaredVersionOfAnyLooseIndex()
    {
        using var stream = new MemoryStream();
        Oxygen.Managed.Assets.Testing.LooseCookedIndexFixture.Write(stream, CreateSampleDocument());
        BinaryPrimitives.WriteUInt16LittleEndian(stream.GetBuffer().AsSpan(8, 2), 4);

        _ = LooseCookedIndex.ReadFormatVersion(stream.ToArray()).Should().Be((ushort)4);
        _ = LooseCookedIndex.ReadFormatVersion(new byte[LooseCookedIndex.HeaderSize]).Should().BeNull();
    }

    [TestMethod]
    [DataRow(1)]
    [DataRow(2)]
    [DataRow(3)]
    [DataRow(4)]
    [DataRow(5)]
    [DataRow(65535)]
    public void Read_WithUnsupportedVersion_ShouldThrow(int version)
    {
        using var stream = new MemoryStream();
        Oxygen.Managed.Assets.Testing.LooseCookedIndexFixture.Write(stream, CreateSampleDocument());
        BinaryPrimitives.WriteUInt16LittleEndian(stream.GetBuffer().AsSpan(8, 2), checked((ushort)version));
        stream.Position = 0;
        Action read = () => _ = LooseCookedIndex.Read(stream);
        _ = read.Should().Throw<InvalidDataException>();
    }

    [TestMethod]
    [DataRow(6, 0x40)]
    [DataRow(8, 0x00)]
    public void Read_WithInvalidSourceIdentity_ShouldThrow(int byteOffset, int value)
    {
        using var stream = new MemoryStream();
        Oxygen.Managed.Assets.Testing.LooseCookedIndexFixture.Write(stream, CreateSampleDocument());
        stream.GetBuffer()[16 + byteOffset] = checked((byte)value);
        stream.Position = 0;
        Action read = () => _ = LooseCookedIndex.Read(stream);
        _ = read.Should().Throw<InvalidDataException>();
    }

    [TestMethod]
    public void Read_WithUnknownFlagsBits_ShouldThrow()
    {
        var doc = CreateSampleDocument();

        byte[] bytes;
        using (var ms = new MemoryStream())
        {
            Oxygen.Managed.Assets.Testing.LooseCookedIndexFixture.Write(ms, doc);
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
    public void Read_WithObsoleteAssetEntrySize_ShouldThrow()
    {
        using var stream = new MemoryStream();
        Oxygen.Managed.Assets.Testing.LooseCookedIndexFixture.Write(stream, CreateSampleDocument());
        BinaryPrimitives.WriteUInt32LittleEndian(stream.GetBuffer().AsSpan(60, 4), 81);
        stream.Position = 0;

        Action read = () => _ = LooseCookedIndex.Read(stream);
        _ = read.Should().Throw<InvalidDataException>();
    }

    [TestMethod]
    public void Read_WithStringOffsetOutsideTable_ShouldThrow()
    {
        var doc = CreateSampleDocument();

        byte[] bytes;
        using (var ms = new MemoryStream())
        {
            Oxygen.Managed.Assets.Testing.LooseCookedIndexFixture.Write(ms, doc);
            bytes = ms.ToArray();
        }

        // Header layout:
        // - string_table_size at offset 40 (u64)
        // - asset_entries_offset at offset 48 (u64)
        var stringTableSize = BinaryPrimitives.ReadUInt64LittleEndian(bytes.AsSpan(40, 8));
        var assetEntriesOffset = BinaryPrimitives.ReadUInt64LittleEndian(bytes.AsSpan(48, 8));

        // Asset entry layout:
        // - desc_rel_offset at +17 (u32)
        // Set it to an offset beyond the string table.
        var firstAssetDescOffsetField = checked((int)assetEntriesOffset) + 17;
        BinaryPrimitives.WriteUInt32LittleEndian(
            bytes.AsSpan(firstAssetDescOffsetField, 4),
            checked((uint)stringTableSize + 1));

        using var readStream = new MemoryStream(bytes, writable: false);
        Action act = () => _ = LooseCookedIndex.Read(readStream);
        _ = act.Should().Throw<InvalidDataException>();
    }

    [TestMethod]
    public void Write_WithoutFileRecords_ShouldStillPlaceFileRecordsAfterAssetEntries()
    {
        var doc = CreateSampleDocument() with { Files = [] };

        using var ms = new MemoryStream();
        Oxygen.Managed.Assets.Testing.LooseCookedIndexFixture.Write(ms, doc);
        var bytes = ms.ToArray();

        var assetEntriesOffset = BinaryPrimitives.ReadUInt64LittleEndian(bytes.AsSpan(48, 8));
        var assetCount = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(56, 4));
        var fileRecordsOffset = BinaryPrimitives.ReadUInt64LittleEndian(bytes.AsSpan(64, 8));
        var fileRecordCount = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(72, 4));

        _ = fileRecordCount.Should().Be(0);
        _ = fileRecordsOffset.Should().BeGreaterThanOrEqualTo(
            assetEntriesOffset + (assetCount * (ulong)LooseCookedIndex.AssetEntrySize));

        ms.Position = 0;
        _ = LooseCookedIndex.Read(ms).Files.Should().BeEmpty();
    }

    [TestMethod]
    public void Read_WhenFileRecordsOffsetPrecedesAssetEntriesEnd_ShouldThrow()
    {
        var doc = CreateSampleDocument() with { Files = [] };

        byte[] bytes;
        using (var ms = new MemoryStream())
        {
            Oxygen.Managed.Assets.Testing.LooseCookedIndexFixture.Write(ms, doc);
            bytes = ms.ToArray();
        }

        BinaryPrimitives.WriteUInt64LittleEndian(bytes.AsSpan(64, 8), 0);

        using var readStream = new MemoryStream(bytes, writable: false);
        Action act = () => _ = LooseCookedIndex.Read(readStream);
        _ = act.Should().Throw<InvalidDataException>();
    }

    [TestMethod]
    public void Read_TextureDescriptor_ShouldKeepItsVirtualPath()
    {
        var texture = TextureDescriptor("/Content/Textures/Sky.otex");
        var doc = CreateSampleDocument() with { Files = [texture] };

        using var ms = new MemoryStream();
        Oxygen.Managed.Assets.Testing.LooseCookedIndexFixture.Write(ms, doc);
        ms.Position = 0;

        var file = LooseCookedIndex.Read(ms).Files.Should().ContainSingle().Subject;
        _ = file.Kind.Should().Be(FileKind.TextureDescriptor);
        _ = file.VirtualPath.Should().Be("/Content/Textures/Sky.otex");
    }

    [TestMethod]
    [DataRow(FileKind.TextureDescriptor, null)]
    [DataRow(FileKind.BuffersData, "/Content/Textures/Sky.otex")]
    [DataRow(FileKind.TextureDescriptor, "/Content/Materials/Wood.mat")]
    public void Read_WithInvalidFileVirtualPath_ShouldThrow(FileKind kind, string? virtualPath)
    {
        var file = TextureDescriptor(virtualPath) with { Kind = kind };
        var doc = CreateSampleDocument() with { Files = [file] };

        using var ms = new MemoryStream();
        Oxygen.Managed.Assets.Testing.LooseCookedIndexFixture.Write(ms, doc);
        ms.Position = 0;

        Action read = () => _ = LooseCookedIndex.Read(ms);
        _ = read.Should().Throw<InvalidDataException>();
    }

    [TestMethod]
    public void Read_AuxiliaryFileWithProducer_ShouldKeepItsSource()
    {
        var sidecar = new FileRecord(
            Kind: FileKind.Auxiliary,
            RelativePath: "Buffers/Hull.obuf",
            Size: 64,
            Sha256: System.Security.Cryptography.SHA256.HashData("sidecar"u8),
            SourcePath: "/Content/Ship/Hull.buffers.json");
        var doc = CreateSampleDocument() with { Files = [sidecar] };

        using var ms = new MemoryStream();
        Oxygen.Managed.Assets.Testing.LooseCookedIndexFixture.Write(ms, doc);
        ms.Position = 0;

        var file = LooseCookedIndex.Read(ms).Files.Should().ContainSingle().Subject;
        _ = file.VirtualPath.Should().BeNull();
        _ = file.SourcePath.Should().Be("/Content/Ship/Hull.buffers.json", "a re-cook of its producer retires the file");
    }

    [TestMethod]
    public void Read_NamedAuxiliaryFile_ShouldKeepItsVirtualPath()
    {
        var sidecar = new FileRecord(
            Kind: FileKind.Auxiliary,
            RelativePath: "Models/Ship.glb/Hull.lod0.index.9f549650eea1e253.obuf",
            Size: 64,
            Sha256: System.Security.Cryptography.SHA256.HashData("sidecar"u8),
            VirtualPath: "/Content/Models/Ship.glb/Hull.lod0.index.obuf",
            SourcePath: "/Content/Models/Ship.glb");
        var doc = CreateSampleDocument() with { Files = [sidecar] };

        using var ms = new MemoryStream();
        Oxygen.Managed.Assets.Testing.LooseCookedIndexFixture.Write(ms, doc);
        ms.Position = 0;

        var file = LooseCookedIndex.Read(ms).Files.Should().ContainSingle().Subject;
        _ = file.VirtualPath.Should().Be("/Content/Models/Ship.glb/Hull.lod0.index.obuf", "a buffer sidecar is referenced by it");
        _ = file.SourcePath.Should().Be("/Content/Models/Ship.glb");
    }

    [TestMethod]
    public void Read_TableFileWithProducer_ShouldThrow()
    {
        var doc = CreateSampleDocument();
        doc = doc with { Files = [doc.Files[0] with { SourcePath = "/Content/Ship/Ship.glb" }, doc.Files[1]] };

        using var ms = new MemoryStream();
        Oxygen.Managed.Assets.Testing.LooseCookedIndexFixture.Write(ms, doc);
        ms.Position = 0;

        Action read = () => _ = LooseCookedIndex.Read(ms);
        _ = read.Should().Throw<InvalidDataException>();
    }

    [TestMethod]
    public void Read_AssetWithoutProducingSource_ShouldThrow()
    {
        using var ms = new MemoryStream();
        Oxygen.Managed.Assets.Testing.LooseCookedIndexFixture.Write(ms, CreateSampleDocument());
        var assetEntries = checked((int)BinaryPrimitives.ReadUInt64LittleEndian(ms.GetBuffer().AsSpan(48, 8)));
        BinaryPrimitives.WriteUInt32LittleEndian(ms.GetBuffer().AsSpan(assetEntries + 25, 4), 0);
        ms.Position = 0;

        Action read = () => _ = LooseCookedIndex.Read(ms);
        _ = read.Should().Throw<InvalidDataException>();
    }

    private static FileRecord TextureDescriptor(string? virtualPath)
        => new(
            Kind: FileKind.TextureDescriptor,
            RelativePath: "Textures/Sky.otex",
            Size: 51,
            Sha256: System.Security.Cryptography.SHA256.HashData("texture"u8),
            VirtualPath: virtualPath);

    [TestMethod]
    public void WriteThenRead_ShouldRoundTripCommitRetiredFilesAndRequirements()
    {
        var engineKey = new AssetKey(0x4142434445464748UL, 0x5152535455565758UL);
        var document = CreateSampleDocument() with
        {
            Commit = 5,
            Retired =
            [
                new RetiredFile(FileKind.TextureDescriptor, "resources/Old.otex", 4),
                new RetiredFile(FileKind.AssetDescriptor, "assets/Materials/Old.mat", 5),
            ],
            Requirements =
            [
                new AssetRequirement(0, "Engine", IsTexture: false, engineKey, AssetType: 1),
                new AssetRequirement(1, "Props", IsTexture: true, engineKey, AssetType: 0),
            ],
        };

        var read = RoundTrip(document);

        _ = read.Commit.Should().Be(5);
        _ = read.Retired.Should().Equal(document.Retired);
        _ = read.Requirements.Should().Equal(document.Requirements);
    }

    [TestMethod]
    [DataRow(FileKind.BuffersData, 3UL, DisplayName = "a data file is never retired")]
    [DataRow(FileKind.Unknown, 3UL, DisplayName = "an unknown kind")]
    [DataRow(FileKind.Auxiliary, 0UL, DisplayName = "no commit")]
    [DataRow(FileKind.Auxiliary, 6UL, DisplayName = "a commit past the index")]
    public void Read_WithAnImpossibleRetiredFile_ShouldThrow(FileKind kind, ulong retiredAt)
    {
        var document = CreateSampleDocument() with { Commit = 5, Retired = [new RetiredFile(kind, "resources/Old.bin", retiredAt)] };

        var read = () => RoundTrip(document);

        _ = read.Should().Throw<InvalidDataException>();
    }

    [TestMethod]
    public void Read_WithARetiredPathTheIndexLists_ShouldThrow()
    {
        var document = CreateSampleDocument() with { Commit = 5, Retired = [new RetiredFile(FileKind.BuffersTable, "resources/buffers.table", 3)] };

        var read = () => RoundTrip(document);

        _ = read.Should().Throw<InvalidDataException>();
    }

    [TestMethod]
    public void Read_WithRequirementsOutOfAssetOrder_ShouldThrow()
    {
        var key = new AssetKey(1, 2);
        var document = CreateSampleDocument() with
        {
            Requirements = [new AssetRequirement(1, "Engine", IsTexture: false, key, 1), new AssetRequirement(0, "Engine", IsTexture: false, key, 1)],
        };

        var read = () => RoundTrip(document);

        _ = read.Should().Throw<InvalidDataException>();
    }

    [TestMethod]
    public void Read_WithADigestOnADataFile_ShouldThrow()
    {
        var sample = CreateSampleDocument();
        var document = sample with
        {
            Files = [.. sample.Files.Select(static file => file.Kind == FileKind.BuffersData ? file with { Sha256 = System.Security.Cryptography.SHA256.HashData("data"u8) } : file)],
        };

        var read = () => RoundTrip(document);

        _ = read.Should().Throw<InvalidDataException>().WithMessage("*records no digest*");
    }

    [TestMethod]
    public void Read_WithNoDigestOnATable_ShouldThrow()
    {
        var sample = CreateSampleDocument();
        var document = sample with
        {
            Files = [.. sample.Files.Select(static file => file.Kind == FileKind.BuffersTable ? file with { Sha256 = new byte[LooseCookedIndex.Sha256Size] } : file)],
        };

        var read = () => RoundTrip(document);

        _ = read.Should().Throw<InvalidDataException>().WithMessage("*missing its SHA-256*");
    }

    [TestMethod]
    [DataRow(LooseCookedIndex.FileName, true)]
    [DataRow(LooseCookedIndex.GenerationLeaseFileName, true)]
    [DataRow(LooseCookedIndex.WriterLockFileName, true)]
    [DataRow(LooseCookedIndex.LeasesFolderName, true)]
    [DataRow(".leases/3.lock", true)]
    [DataRow(".leasesish/3.lock", false)]
    [DataRow("resources/buffers.table", false)]
    public void IsReservedPath_ShouldKnowTheRootsOwnFiles(string path, bool reserved)
        => _ = LooseCookedIndex.IsReservedPath(path).Should().Be(reserved);

    private static Document RoundTrip(Document document)
    {
        using var stream = new MemoryStream();
        Oxygen.Managed.Assets.Testing.LooseCookedIndexFixture.Write(stream, document);
        stream.Position = 0;
        return LooseCookedIndex.Read(stream);
    }

    private static Document CreateSampleDocument()
    {
        var shaA = System.Security.Cryptography.SHA256.HashData("descriptor-a"u8);
        var shaB = System.Security.Cryptography.SHA256.HashData("descriptor-b"u8);
        var shaF1 = System.Security.Cryptography.SHA256.HashData("file-1"u8);

        return new Document(
            ContentVersion: 7,
            Flags: IndexFeatures.HasVirtualPaths | IndexFeatures.HasFileRecords,
            SourceGuid: Guid.ParseExact("01234567-89ab-7cde-8fed-ba9876543210", "D"),
            Assets:
            [
                new AssetEntry(
                    AssetKey: new AssetKey(0x0102030405060708UL, 0x1112131415161718UL),
                    DescriptorRelativePath: "assets/Materials/Wood.mat",
                    VirtualPath: "/Content/Materials/Wood.mat",
                    AssetType: 2,
                    DescriptorSize: 123,
                    DescriptorSha256: shaA)
                {
                    References = new(0, ResourceCount: 1, KeyCount: 1),
                    SourcePath = "/Content/Materials/Wood.mat.json",
                },
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
                    Kind: FileKind.BuffersData,
                    RelativePath: "resources/buffers.data",
                    Size: 2000,
                    Sha256: new byte[LooseCookedIndex.Sha256Size]),
            ]);
    }
}
