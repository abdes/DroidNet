// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using AwesomeAssertions;
using Oxygen.Assets.Cook;
using Oxygen.Assets.Import;
using Oxygen.Assets.Import.Materials;
using Oxygen.Assets.Persistence.LooseCooked.V1;

namespace Oxygen.Assets.Tests;

[TestClass]
public sealed class LooseCookedBuildServiceTests
{
    [TestMethod]
    public async Task BuildIndexesAsync_ShouldWriteContainerIndexForCookedMaterial()
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

        var import = new ImportService(
            registry,
            fileAccessFactory: _ => files,
            identityPolicyFactory: static () => new FixedIdentityPolicy(new AssetKey(1, 2)));

        var request = new ImportRequest(
            ProjectRoot: "C:/Fake",
            Inputs: [new ImportInput(SourcePath: sourcePath, MountPoint: "Content")],
            Options: new ImportOptions(FailFast: true));

        var imported = await import.ImportAsync(request, CancellationToken.None).ConfigureAwait(false);
        _ = imported.Succeeded.Should().BeTrue();
        _ = imported.Imported.Should().ContainSingle();

        var build = new LooseCookedBuildService(fileAccessFactory: _ => files);
        await build.BuildIndexAsync("C:/Fake", imported.Imported, CancellationToken.None).ConfigureAwait(false);

        _ = files.TryGet(".cooked/Content/container.index.bin", out var indexBytes).Should().BeTrue();
        var ms = new MemoryStream(indexBytes);
        Document doc;
        try
        {
            doc = LooseCookedIndex.Read(ms);
        }
        finally
        {
            await ms.DisposeAsync().ConfigureAwait(false);
        }

        _ = doc.Assets.Should().ContainSingle();
        var entry = doc.Assets[0];

        _ = entry.AssetKey.Should().Be(new AssetKey(1, 2));
        _ = entry.VirtualPath.Should().Be("/Content/Materials/Wood.omat");
        _ = entry.DescriptorRelativePath.Should().Be("Materials/Wood.omat");
        _ = entry.AssetType.Should().Be(1);
        _ = entry.DescriptorSize.Should().Be(256);

        _ = files.TryGet(".cooked/Content/Materials/Wood.omat", out var cookedBytes).Should().BeTrue();
        _ = entry.DescriptorSha256.Span.ToArray().Should().Equal(LooseCookedIndex.ComputeSha256(cookedBytes));
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
