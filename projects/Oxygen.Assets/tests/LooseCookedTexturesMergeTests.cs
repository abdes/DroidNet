// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using AwesomeAssertions;
using Oxygen.Assets.Cook;
using Oxygen.Assets.Import;
using Oxygen.Assets.Import.Textures;
using Oxygen.Assets.Persistence.LooseCooked.V1;
using SixLabors.ImageSharp;
using SixLabors.ImageSharp.PixelFormats;

namespace Oxygen.Assets.Tests;

[TestClass]
public sealed class LooseCookedTexturesMergeTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task BuildIndexAsync_Twice_ShouldAppendTextures_AndPreserveExistingTableData()
    {
        var files = new InMemoryImportFileAccess(this.TestContext);

        // Seed two distinct intermediate snapshots.
        files.AddBytes(".imported/Content/Textures/A.png", CreatePngBytes(2, 2));
        files.AddBytes(".imported/Content/Textures/B.png", CreatePngBytes(3, 1));

        files.AddBytes(
            "Content/Textures/A.otex.json",
            CreateTextureSourceJson("Content/Textures/A.png", colorSpace: "Srgb", mipMode: "None"));

        files.AddBytes(
            "Content/Textures/B.otex.json",
            CreateTextureSourceJson("Content/Textures/B.png", colorSpace: "Srgb", mipMode: "None"));

        var a = new ImportedAsset(
            AssetKey: new AssetKey(1, 0),
            VirtualPath: "/Content/Textures/A.otex",
            AssetType: "Texture",
            Source: new ImportedAssetSource("Content/Textures/A.png", ReadOnlyMemory<byte>.Empty, DateTimeOffset.UtcNow),
            Dependencies: [],
            GeneratedSourcePath: "Content/Textures/A.otex.json",
            IntermediateCachePath: ".imported/Content/Textures/A.png",
            Payload: null);

        var b = new ImportedAsset(
            AssetKey: new AssetKey(2, 0),
            VirtualPath: "/Content/Textures/B.otex",
            AssetType: "Texture",
            Source: new ImportedAssetSource("Content/Textures/B.png", ReadOnlyMemory<byte>.Empty, DateTimeOffset.UtcNow),
            Dependencies: [],
            GeneratedSourcePath: "Content/Textures/B.otex.json",
            IntermediateCachePath: ".imported/Content/Textures/B.png",
            Payload: null);

        await LooseCookedBuildService.BuildIndexAsync(files, [a], CancellationToken.None).ConfigureAwait(false);

        _ = files.TryGet(".cooked/Content/resources/textures.table", out var table1).Should().BeTrue();
        _ = files.TryGet(".cooked/Content/resources/textures.data", out var data1).Should().BeTrue();
        _ = table1.Length.Should().Be(40 * 2);

        await LooseCookedBuildService.BuildIndexAsync(files, [b], CancellationToken.None).ConfigureAwait(false);

        _ = files.TryGet(".cooked/Content/resources/textures.table", out var table2).Should().BeTrue();
        _ = files.TryGet(".cooked/Content/resources/textures.data", out var data2).Should().BeTrue();

        // Existing entry must not be overwritten; new entry appended.
        _ = table2.Length.Should().Be(40 * 3);
        _ = data2.Length.Should().BeGreaterThan(data1.Length);

        // First entry bytes should be preserved exactly (reserved + first desc).
        _ = table2.AsSpan(0, 40 * 2).ToArray().Should().Equal(table1);
    }

    private static byte[] CreateTextureSourceJson(string sourceImage, string colorSpace, string mipMode)
    {
        var data = new TextureSourceData(
            Schema: "oxygen.texture.v1",
            Name: null,
            SourceImage: sourceImage,
            ColorSpace: colorSpace,
            TextureType: "Texture2D",
            MipPolicy: new TextureMipPolicyData(Mode: mipMode),
            RuntimeFormat: new TextureRuntimeFormatData(Format: "R8G8B8A8_UNorm", Compression: "None"),
            Imported: null);

        using var ms = new MemoryStream();
        TextureSourceWriter.Write(ms, data);
        return ms.ToArray();
    }

    private static byte[] CreatePngBytes(int width, int height)
    {
        using var img = new Image<Rgba32>(width, height);
        using var ms = new MemoryStream();
        img.SaveAsPng(ms);
        return ms.ToArray();
    }

    private sealed class InMemoryImportFileAccess : IImportFileAccess
    {
        private readonly ConcurrentDictionary<string, Entry> files = new(StringComparer.Ordinal);
        private readonly TestContext testContext;

        public InMemoryImportFileAccess(TestContext testContext)
        {
            this.testContext = testContext;
        }

        public void AddBytes(string relativePath, byte[] bytes)
        {
            this.files[relativePath] = new Entry(bytes, DateTimeOffset.UtcNow);
        }

        public bool TryGet(string relativePath, out byte[] bytes)
        {
            if (this.files.TryGetValue(relativePath, out var entry))
            {
                bytes = entry.Bytes;
                return true;
            }

            bytes = [];
            return false;
        }

        public ValueTask<ImportFileMetadata> GetMetadataAsync(string sourcePath, CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();

            return !this.files.TryGetValue(sourcePath, out var entry)
                ? throw new FileNotFoundException("Missing file.", sourcePath)
                : ValueTask.FromResult(new ImportFileMetadata(
                    ByteLength: entry.Bytes.Length,
                    LastWriteTimeUtc: entry.LastWriteTimeUtc));
        }

        public ValueTask<ReadOnlyMemory<byte>> ReadHeaderAsync(string sourcePath, int maxBytes, CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();

            if (!this.files.TryGetValue(sourcePath, out var entry))
            {
                throw new FileNotFoundException("Missing file.", sourcePath);
            }

            var len = Math.Min(maxBytes, entry.Bytes.Length);
            return ValueTask.FromResult<ReadOnlyMemory<byte>>(entry.Bytes.AsMemory(0, len));
        }

        public ValueTask<ReadOnlyMemory<byte>> ReadAllBytesAsync(string sourcePath, CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();

            return !this.files.TryGetValue(sourcePath, out var entry)
                ? throw new FileNotFoundException("Missing file.", sourcePath)
                : ValueTask.FromResult<ReadOnlyMemory<byte>>(entry.Bytes);
        }

        public ValueTask WriteAllBytesAsync(string relativePath, ReadOnlyMemory<byte> bytes, CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            this.files[relativePath] = new Entry(bytes.ToArray(), DateTimeOffset.UtcNow);
            this.testContext.WriteLine($"[InMemoryImportFileAccess] Write {relativePath} ({bytes.Length} bytes)");
            return ValueTask.CompletedTask;
        }

        private sealed record Entry(byte[] Bytes, DateTimeOffset LastWriteTimeUtc);
    }
}
