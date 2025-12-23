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
    public async Task ImportAsync_ShouldWriteCookedOmatAndReturnImportedAsset()
    {
        const string sourcePath = "Content/Materials/Wood.omat.json";
        var json = """
        {
          "Schema": "oxygen.material.v1",
          "Type": "PBR",
          "Name": "Wood",
          "PbrMetallicRoughness": { "BaseColorFactor": [1, 1, 1, 1] }
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

        _ = results.Should().HaveCount(1);
        var asset = results[0];
        _ = asset.AssetType.Should().Be("Material");
        _ = asset.VirtualPath.Should().Be("/Content/Materials/Wood.omat");
        _ = asset.AssetKey.Should().Be(identity.FixedKey);

        var cookedPath = ".cooked/Content/Materials/Wood.omat";
        _ = files.TryGet(cookedPath, out var cooked).Should().BeTrue();
        _ = cooked.Length.Should().Be(256);

        var expectedHash = SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(json));
        _ = asset.Source.SourceHashSha256.Span.ToArray().Should().Equal(expectedHash);

        _ = diagnostics.ToList().Should().BeEmpty();
    }

    [TestMethod]
    public async Task ImportAsync_ShouldHonorExplicitVirtualPath()
    {
        const string sourcePath = "Content/Materials/Stone.omat.json";
        const string virtualPath = "/Content/Materials/Renamed.omat";

        var json = """
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

        _ = results.Should().HaveCount(1);
        _ = results[0].VirtualPath.Should().Be(virtualPath);

        _ = files.TryGet(".cooked" + virtualPath, out _).Should().BeTrue();
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

    private sealed class FixedIdentityPolicy : IAssetIdentityPolicy
    {
        public FixedIdentityPolicy(AssetKey key) => this.FixedKey = key;

        public AssetKey FixedKey { get; }

        public AssetKey GetOrCreateAssetKey(string virtualPath, string assetType)
        {
            _ = virtualPath;
            _ = assetType;
            return this.FixedKey;
        }
    }

    private sealed class InMemoryImportFileAccess : IImportFileAccess
    {
        private readonly ConcurrentDictionary<string, byte[]> files = new(StringComparer.Ordinal);

        public void AddUtf8(string relativePath, string text)
        {
            ArgumentNullException.ThrowIfNull(relativePath);
            ArgumentNullException.ThrowIfNull(text);
            this.files[relativePath] = System.Text.Encoding.UTF8.GetBytes(text);
        }

        public bool TryGet(string relativePath, out byte[] bytes)
            => this.files.TryGetValue(relativePath, out bytes!);

        public ValueTask<ReadOnlyMemory<byte>> ReadHeaderAsync(string sourcePath, int maxBytes, CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            _ = maxBytes;

            if (!this.files.TryGetValue(sourcePath, out var bytes))
            {
                throw new FileNotFoundException("Missing file.", sourcePath);
            }

            var len = Math.Min(maxBytes, bytes.Length);
            return ValueTask.FromResult<ReadOnlyMemory<byte>>(bytes.AsMemory(0, len));
        }

        public ValueTask<ReadOnlyMemory<byte>> ReadAllBytesAsync(string sourcePath, CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();

            if (!this.files.TryGetValue(sourcePath, out var bytes))
            {
                throw new FileNotFoundException("Missing file.", sourcePath);
            }

            return ValueTask.FromResult<ReadOnlyMemory<byte>>(bytes);
        }

        public ValueTask WriteAllBytesAsync(string relativePath, ReadOnlyMemory<byte> bytes, CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            this.files[relativePath] = bytes.ToArray();
            return ValueTask.CompletedTask;
        }
    }
}
