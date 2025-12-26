// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Security.Cryptography;
using AwesomeAssertions;
using Oxygen.Assets.Import;
using Oxygen.Assets.Import.Materials;
using Oxygen.Assets.Persistence.LooseCooked.V1;

namespace Oxygen.Assets.Tests;

[TestClass]
public sealed class MaterialSourceImporterTests
{
    [TestMethod]
    public async Task ImportAsync_ShouldReturnImportedAsset()
    {
        const string sourcePath = "Content/Materials/Wood.omat.json";
        const string json = """
        {
          "Schema": "oxygen.material.v1",
          "Type": "PBR",
          "Name": "Wood",
                    "PbrMetallicRoughness": {
                        "BaseColorFactor": [1, 1, 1, 1],
                        "BaseColorTexture": { "Source": "asset:///Content/Textures/Wood_BaseColor.png" },
                        "MetallicRoughnessTexture": { "Source": "asset:///Content/Textures/Wood_MR.png" }
                    },
                    "NormalTexture": { "Source": "asset:///Content/Textures/Wood_Normal.png", "Scale": 1.0 },
                    "OcclusionTexture": { "Source": "asset:///Content/Textures/Wood_AO.png", "Strength": 1.0 }
        }
        """;

        var files = new InMemoryImportFileAccess();
        files.AddUtf8(sourcePath, json);

        var identity = new FixedIdentityPolicy(new AssetKey(0x1111111111111111, 0x2222222222222222));
        var diagnostics = new ImportDiagnostics();
        var options = new ImportOptions(FailFast: true);

        var importer = new MaterialSourceImporter();
        var context = new ImportContext(
            Files: files,
            Input: new ImportInput(SourcePath: sourcePath, MountPoint: "Content"),
            Identity: identity,
            Options: options,
            Diagnostics: diagnostics);

        var results = await importer.ImportAsync(context, CancellationToken.None).ConfigureAwait(false);

        _ = results.Should().ContainSingle();
        var asset = results[0];
        _ = asset.AssetType.Should().Be("Material");
        _ = asset.VirtualPath.Should().Be("/Content/Materials/Wood.omat");
        _ = asset.AssetKey.Should().Be(identity.FixedKey);

        var expectedHash = SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(json));
        _ = asset.Source.SourceHashSha256.Span.ToArray().Should().Equal(expectedHash);

        _ = asset.Dependencies.Should().Equal(
            new ImportedDependency(sourcePath, ImportedDependencyKind.SourceFile),
            new ImportedDependency(sourcePath + ".import.json", ImportedDependencyKind.Sidecar),
            new ImportedDependency("Content/Textures/Wood_AO.png", ImportedDependencyKind.ReferencedResource),
            new ImportedDependency("Content/Textures/Wood_BaseColor.png", ImportedDependencyKind.ReferencedResource),
            new ImportedDependency("Content/Textures/Wood_MR.png", ImportedDependencyKind.ReferencedResource),
            new ImportedDependency("Content/Textures/Wood_Normal.png", ImportedDependencyKind.ReferencedResource));

        _ = diagnostics.ToList().Should().BeEmpty();
    }

    [TestMethod]
    public async Task ImportAsync_ShouldHonorExplicitVirtualPath()
    {
        const string sourcePath = "Content/Materials/Stone.omat.json";
        const string virtualPath = "/Content/Materials/Renamed.omat";

        const string json = """
        {
          "Schema": "oxygen.material.v1",
          "Type": "PBR",
          "Name": "Stone"
        }
        """;

        var files = new InMemoryImportFileAccess();
        files.AddUtf8(sourcePath, json);

        var identity = new FixedIdentityPolicy(new AssetKey(1, 2));
        var importer = new MaterialSourceImporter();
        var context = new ImportContext(
            Files: files,
            Input: new ImportInput(SourcePath: sourcePath, MountPoint: "Content", VirtualPath: virtualPath),
            Identity: identity,
            Options: new ImportOptions(FailFast: true),
            Diagnostics: new ImportDiagnostics());

        var results = await importer.ImportAsync(context, CancellationToken.None).ConfigureAwait(false);

        _ = results.Should().ContainSingle();
        _ = results[0].VirtualPath.Should().Be(virtualPath);
    }

    [TestMethod]
    public void CanImport_ShouldMatchOmatJsonSuffix()
    {
        var importer = new MaterialSourceImporter();

        _ = importer.CanImport(new ImportProbe(
            SourcePath: "Content/Materials/Wood.omat.json",
            Extension: ".json",
            HeaderBytes: ReadOnlyMemory<byte>.Empty)).Should().BeTrue();

        _ = importer.CanImport(new ImportProbe(
            SourcePath: "Content/Materials/Wood.json",
            Extension: ".json",
            HeaderBytes: ReadOnlyMemory<byte>.Empty)).Should().BeFalse();
    }

    private sealed class FixedIdentityPolicy(AssetKey key) : IAssetIdentityPolicy
    {
        public AssetKey FixedKey { get; } = key;

        public AssetKey GetOrCreateAssetKey(string virtualPath, string assetType)
        {
            _ = virtualPath;
            _ = assetType;
            return this.FixedKey;
        }
    }

    private sealed class InMemoryImportFileAccess : IImportFileAccess
    {
        private readonly ConcurrentDictionary<string, Entry> files = new(StringComparer.Ordinal);

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
            _ = maxBytes;

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
            return ValueTask.CompletedTask;
        }

        private sealed record Entry(byte[] Bytes, DateTimeOffset LastWriteTimeUtc);
    }
}
