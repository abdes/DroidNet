// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Diagnostics.CodeAnalysis;
using System.Text;
using AwesomeAssertions;
using Oxygen.Assets.Catalog;
using Oxygen.Assets.Catalog.Pak;
using Oxygen.Assets.Persistence.LooseCooked.V1;
using Oxygen.Storage.Native;
using Testably.Abstractions.Testing;

namespace Oxygen.Assets.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
public sealed class PakIndexAssetCatalogTests
{
    private const int PakHeaderSize = 64;
    private const int PakFooterSize = 256;
    private const int AssetDirectoryEntrySize = 64;

    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task QueryAsync_ShouldEnumerateAssetsFromPakBrowseIndexVirtualPaths()
    {
        var fs = new MockFileSystem();
        _ = fs.Directory.CreateDirectory(@"C:\Cooked");

        var keys = new[]
        {
            new AssetKey(1, 2),
            new AssetKey(3, 4),
        };

        var paths = new[]
        {
            "/Content/Textures/A.png",
            "/Content/Textures/B.png",
        };

        var pakBytes = CreatePakV1(keys, paths);
        await fs.File.WriteAllBytesAsync(@"C:\Cooked\Engine.pak", pakBytes, this.TestContext.CancellationToken).ConfigureAwait(true);

        var storage = new NativeStorageProvider(fs);
        using var catalog = new PakIndexAssetCatalog(storage, new PakIndexAssetCatalogOptions
        {
            MountPoint = "Engine",
            PakFilePath = @"C:\Cooked\Engine.pak",
        });

        var results = await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);

        results.Select(r => r.Uri).Should().Contain(new Uri("asset:///Content/Textures/A.png"));
        results.Select(r => r.Uri).Should().Contain(new Uri("asset:///Content/Textures/B.png"));
    }

    [TestMethod]
    public async Task QueryAsync_WhenBrowseIndexMissing_ShouldThrow()
    {
        var fs = new MockFileSystem();
        _ = fs.Directory.CreateDirectory(@"C:\Cooked");

        var pakBytes = CreatePakV1(keys: [], virtualPaths: [], includeBrowseIndex: false);
        await fs.File.WriteAllBytesAsync(@"C:\Cooked\Engine.pak", pakBytes, this.TestContext.CancellationToken).ConfigureAwait(true);

        var storage = new NativeStorageProvider(fs);
        using var catalog = new PakIndexAssetCatalog(storage, new PakIndexAssetCatalogOptions
        {
            MountPoint = "Engine",
            PakFilePath = @"C:\Cooked\Engine.pak",
        });

        Func<Task> act = async () => _ = await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        await act.Should().ThrowAsync<InvalidDataException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task QueryAsync_WhenBrowseIndexOutOfBounds_ShouldThrow()
    {
        var fs = new MockFileSystem();
        _ = fs.Directory.CreateDirectory(@"C:\Cooked");

        var pakBytes = CreatePakV1(keys: [], virtualPaths: [], includeBrowseIndex: true);

        // Corrupt browse index metadata: point it past EOF.
        var footerOffset = pakBytes.Length - PakFooterSize;
        var footer = pakBytes.AsSpan(footerOffset, PakFooterSize);
        BinaryPrimitives.WriteUInt64LittleEndian(footer.Slice(124, 8), (ulong)pakBytes.Length + 16);
        BinaryPrimitives.WriteUInt64LittleEndian(footer.Slice(132, 8), 64);

        await fs.File.WriteAllBytesAsync(@"C:\Cooked\Engine.pak", pakBytes, this.TestContext.CancellationToken).ConfigureAwait(true);

        var storage = new NativeStorageProvider(fs);
        using var catalog = new PakIndexAssetCatalog(storage, new PakIndexAssetCatalogOptions
        {
            MountPoint = "Engine",
            PakFilePath = @"C:\Cooked\Engine.pak",
        });

        Func<Task> act = async () => _ = await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        await act.Should().ThrowAsync<InvalidDataException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task QueryAsync_WhenBrowseIndexContainsInvalidVirtualPath_ShouldThrow()
    {
        var fs = new MockFileSystem();
        _ = fs.Directory.CreateDirectory(@"C:\Cooked");

        var keys = new[]
        {
            new AssetKey(1, 2),
        };

        // Create a valid browse index first, then corrupt the first character of the
        // first virtual path in the browse index string table (remove the leading '/').
        var paths = new[]
        {
            "/Content/NoLeadingSlash.asset",
        };

        var pakBytes = CreatePakV1(keys, paths);

        // Browse index is written immediately after the fixed-size directory in this helper.
        var browseIndexOffset = PakHeaderSize + (AssetDirectoryEntrySize * keys.Length);
        var stringTableOffsetInBrowseIndex = 24 + (24 * keys.Length);
        pakBytes[browseIndexOffset + stringTableOffsetInBrowseIndex] = (byte)'C';

        await fs.File.WriteAllBytesAsync(@"C:\Cooked\Engine.pak", pakBytes, this.TestContext.CancellationToken).ConfigureAwait(true);

        var storage = new NativeStorageProvider(fs);
        using var catalog = new PakIndexAssetCatalog(storage, new PakIndexAssetCatalogOptions
        {
            MountPoint = "Engine",
            PakFilePath = @"C:\Cooked\Engine.pak",
        });

        Func<Task> act = async () => _ = await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        await act.Should().ThrowAsync<InvalidDataException>().ConfigureAwait(false);
    }

    private static byte[] CreatePakV1(AssetKey[] keys, string[] virtualPaths, bool includeBrowseIndex = true)
    {
        if (keys.Length != virtualPaths.Length)
        {
            throw new ArgumentException("keys and virtualPaths length must match.", nameof(virtualPaths));
        }

        const int directoryOffset = PakHeaderSize;
        var directorySize = AssetDirectoryEntrySize * keys.Length;

        var browseIndexBytes = includeBrowseIndex ? CreateBrowseIndexV1(keys, virtualPaths) : Array.Empty<byte>();
        var browseIndexOffset = includeBrowseIndex ? PakHeaderSize + directorySize : 0;
        var browseIndexSize = browseIndexBytes.Length;

        var fileSize = PakHeaderSize + directorySize + browseIndexSize + PakFooterSize;

        var buffer = new byte[fileSize];

        // Header magic: "OXPAK\0\0\0"
        Encoding.ASCII.GetBytes("OXPAK\0\0\0").CopyTo(buffer, 0);
        BinaryPrimitives.WriteUInt16LittleEndian(buffer.AsSpan(8, 2), 1); // version
        BinaryPrimitives.WriteUInt16LittleEndian(buffer.AsSpan(10, 2), 0); // content_version

        // Directory entries.
        for (var i = 0; i < keys.Length; i++)
        {
            var entryOffset = directoryOffset + (i * AssetDirectoryEntrySize);
            var entry = buffer.AsSpan(entryOffset, AssetDirectoryEntrySize);

            keys[i].WriteBytes(entry[..16]);
            entry[16] = 1; // asset_type (arbitrary)
            BinaryPrimitives.WriteUInt64LittleEndian(entry.Slice(17, 8), (ulong)entryOffset);
            BinaryPrimitives.WriteUInt64LittleEndian(entry.Slice(25, 8), 0); // desc_offset
            BinaryPrimitives.WriteUInt32LittleEndian(entry.Slice(33, 4), 0); // desc_size
        }

        // Footer (256 bytes) at the end.
        var footerOffset = fileSize - PakFooterSize;
        var footer = buffer.AsSpan(footerOffset, PakFooterSize);

        BinaryPrimitives.WriteUInt64LittleEndian(footer[..8], (ulong)directoryOffset);
        BinaryPrimitives.WriteUInt64LittleEndian(footer.Slice(8, 8), (ulong)directorySize);
        BinaryPrimitives.WriteUInt64LittleEndian(footer.Slice(16, 8), (ulong)keys.Length);

        // Write browse index blob and store its location in the footer reserved bytes.
        if (includeBrowseIndex)
        {
            browseIndexBytes.CopyTo(buffer, browseIndexOffset);
            BinaryPrimitives.WriteUInt64LittleEndian(footer.Slice(124, 8), (ulong)browseIndexOffset);
            BinaryPrimitives.WriteUInt64LittleEndian(footer.Slice(132, 8), (ulong)browseIndexSize);
        }

        // pak_crc32 stays 0 (skip validation), footer magic "OXPAKEND" at the end.
        Encoding.ASCII.GetBytes("OXPAKEND").CopyTo(footer.Slice(PakFooterSize - 8, 8));

        return buffer;
    }

    private static byte[] CreateBrowseIndexV1(AssetKey[] keys, string[] virtualPaths)
    {
        var strings = new List<byte>(capacity: keys.Length * 32);
        var spans = new (uint offset, uint length)[keys.Length];

        for (var i = 0; i < keys.Length; i++)
        {
            var path = virtualPaths[i];
            if (string.IsNullOrEmpty(path) || path[0] != '/')
            {
                throw new ArgumentException("virtualPaths must start with '/'.", nameof(virtualPaths));
            }

            var bytes = Encoding.UTF8.GetBytes(path);
            spans[i] = ((uint)strings.Count, (uint)bytes.Length);
            strings.AddRange(bytes);
        }

        const int headerSize = 24;
        const int entrySize = 24;
        var totalSize = headerSize + (entrySize * keys.Length) + strings.Count;
        var buffer = new byte[totalSize];

        // Magic "OXPAKBIX" (8 bytes)
        Encoding.ASCII.GetBytes("OXPAKBIX").CopyTo(buffer, 0);
        BinaryPrimitives.WriteUInt32LittleEndian(buffer.AsSpan(8, 4), 1); // version
        BinaryPrimitives.WriteUInt32LittleEndian(buffer.AsSpan(12, 4), (uint)keys.Length);
        BinaryPrimitives.WriteUInt32LittleEndian(buffer.AsSpan(16, 4), (uint)strings.Count);
        BinaryPrimitives.WriteUInt32LittleEndian(buffer.AsSpan(20, 4), 0); // reserved

        var entriesOffset = headerSize;
        for (var i = 0; i < keys.Length; i++)
        {
            var entry = buffer.AsSpan(entriesOffset + (i * entrySize), entrySize);
            keys[i].WriteBytes(entry[..16]);
            BinaryPrimitives.WriteUInt32LittleEndian(entry.Slice(16, 4), spans[i].offset);
            BinaryPrimitives.WriteUInt32LittleEndian(entry.Slice(20, 4), spans[i].length);
        }

        strings.CopyTo(buffer, headerSize + (entrySize * keys.Length));
        return buffer;
    }
}
