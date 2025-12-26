// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Collections.Concurrent;
using System.Text;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Assets.Import;
using Oxygen.Assets.Import.Textures;
using Oxygen.Assets.Persistence.LooseCooked.V1;
using SixLabors.ImageSharp;
using SixLabors.ImageSharp.PixelFormats;

namespace Oxygen.Assets.Tests;

[TestClass]
public sealed class ImageTextureImporterTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task ImportAsync_Png_ShouldGenerateOtexJson_AndImportedSnapshot_AndDependencies()
    {
        const string sourcePath = "Content/Textures/Wood.png";

        var files = new InMemoryImportFileAccess(this.TestContext);
        files.AddBytes(sourcePath, CreatePngBytes(width: 4, height: 3));

        var registry = new ImporterRegistry();
        registry.Register(new ImageTextureImporter());

        var import = new ImportService(
            registry,
            fileAccessFactory: _ => files,
            identityPolicyFactory: static () => new SequenceIdentityPolicy());

        var request = new ImportRequest(
            ProjectRoot: "C:/Fake",
            Inputs: [new ImportInput(SourcePath: sourcePath, MountPoint: "Content")],
            Options: new ImportOptions(FailFast: true));

        var result = await import.ImportAsync(request, CancellationToken.None).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = result.Imported.Should().HaveCount(1);

        var asset = result.Imported.Single();
        _ = asset.AssetType.Should().Be("Texture");
        _ = asset.VirtualPath.Should().Be("/Content/Textures/Wood.otex");
        _ = asset.GeneratedSourcePath.Should().Be("Content/Textures/Wood.otex.json");
        _ = asset.IntermediateCachePath.Should().Be(".imported/Content/Textures/Wood.png");

        AssertDependencyKinds(asset, sourcePath);

        _ = files.TryGet(asset.GeneratedSourcePath!, out var otexJsonBytes).Should().BeTrue();
        var json = Encoding.UTF8.GetString(otexJsonBytes);
        _ = json.Should().Contain("\"Schema\"");
        _ = json.Should().Contain("oxygen.texture.v1");
        _ = json.Should().Contain("\"SourceImage\": \"Content/Textures/Wood.png\"");

        using var doc = JsonDocument.Parse(otexJsonBytes);
        _ = doc.RootElement.GetProperty("Imported").GetProperty("Width").GetInt32().Should().Be(4);
        _ = doc.RootElement.GetProperty("Imported").GetProperty("Height").GetInt32().Should().Be(3);

        _ = files.TryGet(asset.IntermediateCachePath!, out var importedBytes).Should().BeTrue();
        _ = importedBytes.Should().Equal(files.Get(sourcePath));

        // Build step ran; ensure descriptor + resources exist.
        _ = files.TryGet(".cooked" + asset.VirtualPath, out var cookedDesc).Should().BeTrue();
        _ = cookedDesc.Length.Should().BePositive();

        _ = files.TryGet(".cooked/Content/resources/textures.table", out var tableBytes).Should().BeTrue();
        _ = tableBytes.Length.Should().Be(40 * 2); // reserved + 1 entry

        _ = files.TryGet(".cooked/Content/resources/textures.data", out var dataBytes).Should().BeTrue();
        _ = dataBytes.Length.Should().BeGreaterThan(0);
    }

    [TestMethod]
    public async Task ImportAsync_Tga_ShouldGenerateOtexJson_AndImportedSnapshot_AndDependencies()
    {
        const string sourcePath = "Content/Textures/Wood.tga";

        var files = new InMemoryImportFileAccess(this.TestContext);
        files.AddBytes(sourcePath, CreateTgaBytes(width: 4, height: 3));

        var registry = new ImporterRegistry();
        registry.Register(new ImageTextureImporter());

        var import = new ImportService(
            registry,
            fileAccessFactory: _ => files,
            identityPolicyFactory: static () => new SequenceIdentityPolicy());

        var request = new ImportRequest(
            ProjectRoot: "C:/Fake",
            Inputs: [new ImportInput(SourcePath: sourcePath, MountPoint: "Content")],
            Options: new ImportOptions(FailFast: true));

        var result = await import.ImportAsync(request, CancellationToken.None).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = result.Imported.Should().HaveCount(1);

        var asset = result.Imported.Single();
        _ = asset.AssetType.Should().Be("Texture");
        _ = asset.VirtualPath.Should().Be("/Content/Textures/Wood.otex");
        _ = asset.GeneratedSourcePath.Should().Be("Content/Textures/Wood.otex.json");
        _ = asset.IntermediateCachePath.Should().Be(".imported/Content/Textures/Wood.tga");

        AssertDependencyKinds(asset, sourcePath);

        _ = files.TryGet(asset.GeneratedSourcePath!, out var otexJsonBytes).Should().BeTrue();
        using var doc = JsonDocument.Parse(otexJsonBytes);
        _ = doc.RootElement.GetProperty("Imported").GetProperty("Width").GetInt32().Should().Be(4);
        _ = doc.RootElement.GetProperty("Imported").GetProperty("Height").GetInt32().Should().Be(3);
        _ = doc.RootElement.GetProperty("SourceImage").GetString().Should().Be(sourcePath);

        _ = files.TryGet(asset.IntermediateCachePath!, out var importedBytes).Should().BeTrue();
        _ = importedBytes.Should().Equal(files.Get(sourcePath));

        _ = files.TryGet(".cooked" + asset.VirtualPath, out var cookedDesc).Should().BeTrue();
        _ = cookedDesc.Length.Should().BePositive();

        _ = files.TryGet(".cooked/Content/resources/textures.table", out var tableBytes).Should().BeTrue();
        _ = tableBytes.Length.Should().Be(40 * 2);

        _ = files.TryGet(".cooked/Content/resources/textures.data", out var dataBytes).Should().BeTrue();
        _ = dataBytes.Length.Should().BeGreaterThan(0);
    }

    [TestMethod]
    public async Task ImportAsync_EditingOtexJson_ShouldAffectCookedTextureEntry_AndDescriptorIndex()
    {
        const string sourcePath = "Content/Textures/Wood.png";

        var files = new InMemoryImportFileAccess(this.TestContext);
        files.AddBytes(sourcePath, CreatePngBytes(width: 4, height: 3));

        var registry = new ImporterRegistry();
        registry.Register(new ImageTextureImporter());

        var import = new ImportService(
            registry,
            fileAccessFactory: _ => files,
            identityPolicyFactory: static () => new SequenceIdentityPolicy());

        var request = new ImportRequest(
            ProjectRoot: "C:/Fake",
            Inputs: [new ImportInput(SourcePath: sourcePath, MountPoint: "Content")],
            Options: new ImportOptions(FailFast: true));

        var first = await import.ImportAsync(request, CancellationToken.None).ConfigureAwait(false);
        _ = first.Succeeded.Should().BeTrue();

        _ = files.TryGet(".cooked/Content/resources/textures.table", out var table1).Should().BeTrue();
        _ = table1.Length.Should().Be(40 * 2);

        var (mipLevels1, format1) = ReadTextureTableEntry(table1, tableIndex: 1);
        _ = mipLevels1.Should().Be(1);
        _ = format1.Should().Be(31); // kRGBA8UNormSRGB

        // Edit the authoring source: switch to Linear + generate mips.
        var original = TextureSourceReader.Read(files.Get("Content/Textures/Wood.otex.json"));
        var edited = original with
        {
            ColorSpace = "Linear",
            MipPolicy = new TextureMipPolicyData(Mode: "Generate"),
        };

        using (var ms = new MemoryStream())
        {
            TextureSourceWriter.Write(ms, edited);
            await files.WriteAllBytesAsync("Content/Textures/Wood.otex.json", ms.ToArray(), CancellationToken.None).ConfigureAwait(false);
        }

        var second = await import.ImportAsync(request, CancellationToken.None).ConfigureAwait(false);
        _ = second.Succeeded.Should().BeTrue();

        _ = files.TryGet(".cooked/Content/resources/textures.table", out var table2).Should().BeTrue();
        _ = table2.Length.Should().Be(40 * 3); // reserved + first + second

        // Ensure prior entry preserved and new entry reflects settings.
        var (mipLevels1After, format1After) = ReadTextureTableEntry(table2, tableIndex: 1);
        _ = mipLevels1After.Should().Be(1);
        _ = format1After.Should().Be(31);

        var (mipLevels2, format2) = ReadTextureTableEntry(table2, tableIndex: 2);
        _ = mipLevels2.Should().Be(3); // 4x3 -> 2x1 -> 1x1
        _ = format2.Should().Be(30); // kRGBA8UNorm

        _ = files.TryGet(".cooked/Content/Textures/Wood.otex", out var cookedDesc).Should().BeTrue();
        _ = ReadOtexTableIndex(cookedDesc).Should().Be(2u);
    }

    private static void AssertDependencyKinds(ImportedAsset asset, string sourcePath)
    {
        _ = asset.Dependencies.Should().Contain(d => d.Kind == ImportedDependencyKind.SourceFile && d.Path == sourcePath);
        _ = asset.Dependencies.Should().Contain(d => d.Kind == ImportedDependencyKind.SourceFile && d.Path == "Content/Textures/Wood.otex.json");
        _ = asset.Dependencies.Should().Contain(d => d.Kind == ImportedDependencyKind.Sidecar && d.Path == sourcePath + ".import.json");
        _ = asset.Dependencies.Should().OnlyContain(d => d.Kind == ImportedDependencyKind.SourceFile || d.Kind == ImportedDependencyKind.Sidecar);
    }

    private static (ushort mipLevels, byte format) ReadTextureTableEntry(byte[] tableBytes, int tableIndex)
    {
        const int entrySize = 40;
        var offset = tableIndex * entrySize;
        var entry = tableBytes.AsSpan(offset, entrySize);
        var mipLevels = BinaryPrimitives.ReadUInt16LittleEndian(entry.Slice(28, 2));
        var format = entry[30];
        return (mipLevels, format);
    }

    private static uint ReadOtexTableIndex(byte[] otexBytes)
    {
        // Magic[4] + Version[4] + TableIndex[4] + Reserved[4]
        if (otexBytes.Length < 12)
        {
            throw new InvalidDataException("Invalid .otex descriptor.");
        }

        return BinaryPrimitives.ReadUInt32LittleEndian(otexBytes.AsSpan(8, 4));
    }

    private static byte[] CreatePngBytes(int width, int height)
    {
        using var img = new Image<Rgba32>(width, height);
        img[0, 0] = new Rgba32(255, 0, 0, 255);

        using var ms = new MemoryStream();
        img.SaveAsPng(ms);
        return ms.ToArray();
    }

    private static byte[] CreateTgaBytes(int width, int height)
    {
        using var img = new Image<Rgba32>(width, height);
        img[0, 0] = new Rgba32(255, 0, 0, 255);

        using var ms = new MemoryStream();
        img.SaveAsTga(ms);
        return ms.ToArray();
    }

    private sealed class SequenceIdentityPolicy : IAssetIdentityPolicy
    {
        private ulong next = 1;

        public AssetKey GetOrCreateAssetKey(string virtualPath, string assetType)
        {
            _ = virtualPath;
            _ = assetType;
            var id = this.next++;
            return new AssetKey(id, 0);
        }
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

        public byte[] Get(string relativePath)
        {
            if (!this.files.TryGetValue(relativePath, out var entry))
            {
                throw new FileNotFoundException("Missing file.", relativePath);
            }

            return entry.Bytes;
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
