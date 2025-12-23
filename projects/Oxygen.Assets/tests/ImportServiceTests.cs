// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using AwesomeAssertions;
using Oxygen.Assets.Import;
using Oxygen.Assets.Import.Materials;
using Oxygen.Assets.Persistence.LooseCooked.V1;

namespace Oxygen.Assets.Tests;

[TestClass]
public sealed class ImportServiceTests
{
    [TestMethod]
    public async Task ImportAsync_ShouldSelectMaterialImporterAndWriteCookedOutput()
    {
        const string sourcePath = "Content/Materials/Wood.omat.json";
        const string json = """
        {
          "Schema": "oxygen.material.v1",
          "Type": "PBR",
          "Name": "Wood"
        }
        """;

        var files = new InMemoryImportFileAccess();
        files.AddUtf8(sourcePath, json);

        var registry = new ImporterRegistry();
        registry.Register(new MaterialSourceImporter());

        var service = new ImportService(
            registry,
            fileAccessFactory: _ => files,
            identityPolicyFactory: static () => new FixedIdentityPolicy(new AssetKey(1, 2)));

        var request = new ImportRequest(
            ProjectRoot: "C:/Fake",
            Inputs: [new ImportInput(SourcePath: sourcePath, MountPoint: "Content")],
            Options: new ImportOptions(FailFast: true));

        var result = await service.ImportAsync(request, CancellationToken.None).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = result.Diagnostics.Should().BeEmpty();
        _ = result.Imported.Should().ContainSingle();
        _ = result.Imported[0].VirtualPath.Should().Be("/Content/Materials/Wood.omat");

        _ = files.TryGet(".cooked/Content/Materials/Wood.omat", out var cooked).Should().BeTrue();
        _ = cooked.Should().HaveCount(256);

        _ = files.TryGet(".cooked/Content/container.index.bin", out var indexBytes).Should().BeTrue();
        var ms = new MemoryStream(indexBytes);
        try
        {
            var doc = LooseCookedIndex.Read(ms);
            _ = doc.Assets.Should().ContainSingle(a => a.VirtualPath == "/Content/Materials/Wood.omat");
        }
        finally
        {
            await ms.DisposeAsync().ConfigureAwait(false);
        }
    }

    [TestMethod]
    public async Task ImportAsync_WhenNoImporterMatches_ShouldReportErrorAndFail()
    {
        var files = new InMemoryImportFileAccess();

        var registry = new ImporterRegistry();
        var service = new ImportService(
            registry,
            fileAccessFactory: _ => files,
            identityPolicyFactory: static () => new FixedIdentityPolicy(new AssetKey(1, 2)));

        var request = new ImportRequest(
            ProjectRoot: "C:/Fake",
            Inputs: [new ImportInput(SourcePath: "Content/Unknown/File.xyz", MountPoint: "Content")],
            Options: new ImportOptions(FailFast: false));

        var result = await service.ImportAsync(request, CancellationToken.None).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.Diagnostics.Should().ContainSingle(d => d.Code == "OXYIMPORT_NO_IMPORTER");
    }

    [TestMethod]
    public async Task ImportAsync_WhenUnchangedAndSidecarAvailable_ShouldNoOp()
    {
        const string sourcePath = "Content/Materials/Wood.omat.json";
        const string json = """
        {
          "Schema": "oxygen.material.v1",
          "Type": "PBR",
          "Name": "Wood"
        }
        """;

        var files = new InMemoryImportFileAccess();
        files.AddUtf8(sourcePath, json);

        var registry = new ImporterRegistry();
        var importer = new CountingImporter(new MaterialSourceImporter());
        registry.Register(importer);

        var service = new ImportService(
            registry,
            fileAccessFactory: _ => files,
            identityPolicyFactory: static (f, input, imp, options) => new SidecarAssetIdentityPolicy(f, input, imp, options));

        var request = new ImportRequest(
            ProjectRoot: "C:/Fake",
            Inputs: [new ImportInput(SourcePath: sourcePath, MountPoint: "Content")],
            Options: new ImportOptions(FailFast: true));

        var first = await service.ImportAsync(request, CancellationToken.None).ConfigureAwait(false);
        _ = first.Succeeded.Should().BeTrue();
        _ = importer.ImportCallCount.Should().Be(1);
        var writesAfterFirst = files.WriteCount;

        var second = await service.ImportAsync(request, CancellationToken.None).ConfigureAwait(false);
        _ = second.Succeeded.Should().BeTrue();
        _ = importer.ImportCallCount.Should().Be(1);
        _ = second.Imported.Should().BeEmpty();
        _ = files.WriteCount.Should().Be(writesAfterFirst);
    }

    [TestMethod]
    public async Task ImportAsync_WhenDependencyChanges_ShouldReimport()
    {
        const string sourcePath = "Content/Materials/Wood.omat.json";
        const string texPath = "Content/Textures/Wood_BaseColor.png";

        const string json = """
        {
          "Schema": "oxygen.material.v1",
          "Type": "PBR",
          "Name": "Wood",
          "PbrMetallicRoughness": {
            "BaseColorTexture": { "Source": "asset://Content/Textures/Wood_BaseColor.png" }
          }
        }
        """;

        var files = new InMemoryImportFileAccess();
        files.AddUtf8(sourcePath, json);
        files.AddUtf8(texPath, "v1");

        var registry = new ImporterRegistry();
        var importer = new CountingImporter(new MaterialSourceImporter());
        registry.Register(importer);

        var service = new ImportService(
            registry,
            fileAccessFactory: _ => files,
            identityPolicyFactory: static (f, input, imp, options) => new SidecarAssetIdentityPolicy(f, input, imp, options));

        var request = new ImportRequest(
            ProjectRoot: "C:/Fake",
            Inputs: [new ImportInput(SourcePath: sourcePath, MountPoint: "Content")],
            Options: new ImportOptions(FailFast: true));

        var first = await service.ImportAsync(request, CancellationToken.None).ConfigureAwait(false);
        _ = first.Succeeded.Should().BeTrue();
        _ = importer.ImportCallCount.Should().Be(1);

        var second = await service.ImportAsync(request, CancellationToken.None).ConfigureAwait(false);
        _ = second.Succeeded.Should().BeTrue();
        _ = importer.ImportCallCount.Should().Be(1);
        _ = second.Imported.Should().BeEmpty();

        files.AddUtf8(texPath, "v2");

        var third = await service.ImportAsync(request, CancellationToken.None).ConfigureAwait(false);
        _ = third.Succeeded.Should().BeTrue();
        _ = importer.ImportCallCount.Should().Be(2);
        _ = third.Imported.Should().NotBeEmpty();
    }

    private sealed class FixedIdentityPolicy(AssetKey key) : IAssetIdentityPolicy
    {
        public AssetKey Key { get; } = key;

        public AssetKey GetOrCreateAssetKey(string virtualPath, string assetType)
        {
            _ = virtualPath;
            _ = assetType;
            return this.Key;
        }
    }

    private sealed class CountingImporter : IAssetImporter
    {
        private readonly IAssetImporter inner;

        public CountingImporter(IAssetImporter inner)
        {
            ArgumentNullException.ThrowIfNull(inner);
            this.inner = inner;
        }

        public int ImportCallCount { get; private set; }

        public string Name => this.inner.Name;

        public int Priority => this.inner.Priority;

        public bool CanImport(ImportProbe probe) => this.inner.CanImport(probe);

        public async Task<IReadOnlyList<ImportedAsset>> ImportAsync(ImportContext context, CancellationToken cancellationToken)
        {
            this.ImportCallCount++;
            return await this.inner.ImportAsync(context, cancellationToken).ConfigureAwait(false);
        }
    }

    private sealed class InMemoryImportFileAccess : IImportFileAccess
    {
        private readonly ConcurrentDictionary<string, Entry> files = new(StringComparer.Ordinal);

        public int WriteCount { get; private set; }

        public void AddUtf8(string relativePath, string text)
        {
            ArgumentNullException.ThrowIfNull(relativePath);
            ArgumentNullException.ThrowIfNull(text);
            this.files[relativePath] = new Entry(System.Text.Encoding.UTF8.GetBytes(text), DateTimeOffset.UtcNow);
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
                // For probe tests, missing file is OK; treat as empty.
                return ValueTask.FromResult<ReadOnlyMemory<byte>>(ReadOnlyMemory<byte>.Empty);
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
            this.WriteCount++;
            this.files[relativePath] = new Entry(bytes.ToArray(), DateTimeOffset.UtcNow);
            return ValueTask.CompletedTask;
        }

        private sealed record Entry(byte[] Bytes, DateTimeOffset LastWriteTimeUtc);
    }
}
