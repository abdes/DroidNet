// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Security.Cryptography;
using AwesomeAssertions;
using Oxygen.Managed.Assets.Import;
using Oxygen.Managed.Assets.Import.Materials;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V1;

namespace Oxygen.Managed.Assets.Tests;

[TestClass]
public sealed class ImportServiceTests
{
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task ImportAsync_ShouldRejectSceneCookingWithoutRepairingExistingCookedFiles(bool failFast)
    {
        const string sourcePath = "Content/Models/Scene.glb";
        const string indexPath = ".cooked/Content/container.index.bin";
        const string tablePath = ".cooked/Content/resources/buffers.table";
        var files = new InMemoryImportFileAccess();
        _ = await SeedStaleIndexAsync(files, tablePath, indexPath, "resources/buffers.table").ConfigureAwait(false);
        files.AddUtf8(sourcePath, "scene source");
        _ = files.TryGet(indexPath, out var originalIndex).Should().BeTrue();
        var writesBefore = files.WriteCount;
        var registry = new ImporterRegistry();
        registry.Register(new SceneOutputImporter());
        var service = new ImportService(
            registry,
            fileAccessFactory: _ => files,
            identityPolicyFactory: static () => new FixedIdentityPolicy(new AssetKey(1, 2)));

        var request = new ImportRequest(
            "C:/Fake",
            [new ImportInput(sourcePath, "Content")],
            new ImportOptions(FailFast: failFast));
        var result = await service.ImportAsync(request, CancellationToken.None).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        var diagnostic = result.Diagnostics.Should().ContainSingle(d => d.Code == "OXYIMPORT_NATIVE_SCENE_COOK_REQUIRED").Which;
        _ = diagnostic.Severity.Should().Be(ImportDiagnosticSeverity.Error);
        _ = diagnostic.SourcePath.Should().Be(sourcePath);
        _ = diagnostic.VirtualPath.Should().Be("/Content/Scenes/Main.oscene");
        _ = files.WriteCount.Should().Be(writesBefore);
        _ = files.TryGet(indexPath, out var unchangedIndex).Should().BeTrue();
        _ = unchangedIndex.Should().Equal(originalIndex);
        _ = files.TryGet(".cooked/Content/Scenes/Main.oscene", out _).Should().BeFalse();
    }

    [TestMethod]
    public async Task ImportAsync_ShouldRejectCachedSceneOutputsInsteadOfReportingSuccessfulNoOp()
    {
        const string sourcePath = "Content/Models/Scene.glb";
        var files = new InMemoryImportFileAccess();
        files.AddUtf8(sourcePath, "scene source");
        var importer = new SceneOutputImporter();
        var registry = new ImporterRegistry();
        registry.Register(importer);
        var service = new ImportService(
            registry,
            fileAccessFactory: _ => files,
            identityPolicyFactory: static (fileAccess, input, selected, options) => new SidecarAssetIdentityPolicy(fileAccess, input, selected, options));
        var request = new ImportRequest("C:/Fake", [new ImportInput(sourcePath, "Content")], new ImportOptions());

        var first = await service.ImportAsync(request, CancellationToken.None).ConfigureAwait(false);
        _ = first.Succeeded.Should().BeFalse();
        _ = first.Diagnostics.Should().ContainSingle(d => d.Code == "OXYIMPORT_NATIVE_SCENE_COOK_REQUIRED");
        var writesAfterFirst = files.WriteCount;
        var second = await service.ImportAsync(request, CancellationToken.None).ConfigureAwait(false);

        _ = importer.ImportCallCount.Should().Be(1);
        _ = second.Succeeded.Should().BeFalse();
        _ = second.Diagnostics.Should().ContainSingle(d => d.Code == "OXYIMPORT_UP_TO_DATE");
        _ = second.Diagnostics.Should().ContainSingle(d => d.Code == "OXYIMPORT_NATIVE_SCENE_COOK_REQUIRED");
        _ = files.WriteCount.Should().Be(writesAfterFirst);
        _ = files.TryGet(".cooked/Content/container.index.bin", out _).Should().BeFalse();
    }

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
        _ = cooked.Should().HaveCount(357);

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
            "BaseColorTexture": { "Source": "asset:///Content/Textures/Wood_BaseColor.png" }
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

    [TestMethod]
    public async Task ImportAsync_WhenFailureOccurs_ShouldRepairIndexFileRecords()
    {
        const string sourcePath = "Content/Models/Bad.glb";
        const string mountPoint = "Content";
        const string buffersTableRelativePath = "resources/buffers.table";
        const string indexPath = ".cooked/Content/container.index.bin";
        const string buffersTablePath = ".cooked/Content/resources/buffers.table";

        var files = new InMemoryImportFileAccess();
        var newBytes = await SeedStaleIndexAsync(files, buffersTablePath, indexPath, buffersTableRelativePath).ConfigureAwait(false);

        var registry = new ImporterRegistry();
        registry.Register(new ThrowingImporter());

        var service = new ImportService(
            registry,
            fileAccessFactory: _ => files,
            identityPolicyFactory: static () => new FixedIdentityPolicy(new AssetKey(1, 2)));

        var request = new ImportRequest(
            ProjectRoot: "C:/Fake",
            Inputs: [new ImportInput(SourcePath: sourcePath, MountPoint: mountPoint)],
            Options: new ImportOptions(FailFast: false));

        var result = await service.ImportAsync(request, CancellationToken.None).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.Diagnostics.Should().ContainSingle(d => d.Code == "OXYIMPORT_INPUT_FAILED");

        _ = files.TryGet(indexPath, out var repairedIndexBytes).Should().BeTrue();
        var msRepaired = new MemoryStream(repairedIndexBytes);
        await using var repairedLifetime = msRepaired.ConfigureAwait(false);
        var repairedDoc = LooseCookedIndex.Read(msRepaired);

        var buffersRecord = repairedDoc.Files.Should()
            .ContainSingle(f => f.RelativePath == buffersTableRelativePath)
            .Which;

        _ = buffersRecord.Size.Should().Be((ulong)newBytes.Length);
        _ = buffersRecord.Sha256.IsEmpty.Should().BeTrue();
    }

    private static async Task<byte[]> SeedStaleIndexAsync(
        InMemoryImportFileAccess files,
        string buffersTablePath,
        string indexPath,
        string buffersTableRelativePath)
    {
        var oldBytes = new byte[96];
        RandomNumberGenerator.Fill(oldBytes);

        var newBytes = new byte[160];
        RandomNumberGenerator.Fill(newBytes);
        await files.WriteAllBytesAsync(buffersTablePath, newBytes, CancellationToken.None).ConfigureAwait(false);

        var doc = new Document(
            ContentVersion: 1,
            Flags: IndexFeatures.None,
            SourceGuid: Guid.ParseExact("01234567-89ab-7cde-8fed-ba9876543210", "D"),
            Assets: Array.Empty<AssetEntry>(),
            Files:
            [
                new FileRecord(
                    Kind: FileKind.BuffersTable,
                    RelativePath: buffersTableRelativePath,
                    Size: (ulong)oldBytes.Length,
                    Sha256: LooseCookedIndex.ComputeSha256(oldBytes)),
            ]);

        var ms = new MemoryStream();
        await using var streamLifetime = ms.ConfigureAwait(false);
        LooseCookedIndex.Write(ms, doc);
        await files.WriteAllBytesAsync(indexPath, ms.ToArray(), CancellationToken.None).ConfigureAwait(false);
        return newBytes;
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

    private sealed class SceneOutputImporter : IAssetImporter
    {
        public string Name => "SceneOutput";

        public int Priority => 0;

        public int ImportCallCount { get; private set; }

        public bool CanImport(ImportProbe probe) => probe.Extension.Equals(".glb", StringComparison.OrdinalIgnoreCase);

        public async Task<IReadOnlyList<ImportedAsset>> ImportAsync(ImportContext context, CancellationToken cancellationToken)
        {
            this.ImportCallCount++;
            var bytes = await context.Files.ReadAllBytesAsync(context.Input.SourcePath, cancellationToken).ConfigureAwait(false);
            var metadata = await context.Files.GetMetadataAsync(context.Input.SourcePath, cancellationToken).ConfigureAwait(false);
            const string virtualPath = "/Content/Scenes/Main.oscene";
            return
            [
                new ImportedAsset(
                    context.Identity.GetOrCreateAssetKey(virtualPath, "Scene"),
                    virtualPath,
                    "Scene",
                    new ImportedAssetSource(context.Input.SourcePath, SHA256.HashData(bytes.Span), metadata.LastWriteTimeUtc),
                    [new ImportedDependency(context.Input.SourcePath, ImportedDependencyKind.SourceFile)]),
            ];
        }
    }

    private sealed class ThrowingImporter : IAssetImporter
    {
        public string Name => "Throwing";

        public int Priority => 0;

        public bool CanImport(ImportProbe probe)
        {
            _ = probe;
            return true;
        }

        public Task<IReadOnlyList<ImportedAsset>> ImportAsync(ImportContext context, CancellationToken cancellationToken)
        {
            _ = context;
            _ = cancellationToken;
            throw new InvalidOperationException("Simulated import failure.");
        }
    }
}
