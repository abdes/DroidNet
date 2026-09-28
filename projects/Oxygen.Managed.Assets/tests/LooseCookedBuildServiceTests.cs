// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using AwesomeAssertions;
using Oxygen.Managed.Assets.Cook;
using Oxygen.Managed.Assets.Import;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V1;

namespace Oxygen.Managed.Assets.Tests;

[TestClass]
public sealed class LooseCookedBuildServiceTests
{
    [TestMethod]
    public async Task BuildIndexAsync_ShouldRejectSceneBatchBeforeAnyMountIsChanged()
    {
        var files = new InMemoryImportFileAccess();
        files.AddUtf8("A/Materials/Wood.omat.json", """{"name":"Wood"}""");
        files.AddUtf8(".cooked/A/Materials/Wood.omat", "previous descriptor");
        files.AddUtf8(".cooked/A/container.index.bin", "previous index");
        var source = new ImportedAssetSource("source", ReadOnlyMemory<byte>.Empty, DateTimeOffset.UnixEpoch);
        ImportedAsset[] assets =
        [
            new(new AssetKey(1, 1), "/A/Materials/Wood.omat", "Material", source, [], GeneratedSourcePath: "A/Materials/Wood.omat.json"),
            new(new AssetKey(2, 2), "/Z/Scenes/Main.oscene", "Scene", source, []),
        ];
        var build = new LooseCookedBuildService(fileAccessFactory: _ => files);
        Func<Task> action = () => build.BuildIndexAsync("C:/Fake", assets, CancellationToken.None);

        _ = await action.Should().ThrowAsync<NotSupportedException>().WithMessage("*native content pipeline*").ConfigureAwait(false);

        _ = files.WriteCount.Should().Be(0);
        _ = files.TryGet(".cooked/A/Materials/Wood.omat", out var descriptor).Should().BeTrue();
        _ = System.Text.Encoding.UTF8.GetString(descriptor).Should().Be("previous descriptor");
        _ = files.TryGet(".cooked/A/container.index.bin", out var index).Should().BeTrue();
        _ = System.Text.Encoding.UTF8.GetString(index).Should().Be("previous index");
        _ = files.TryGet(".cooked/Z/Scenes/Main.oscene", out _).Should().BeFalse();
    }

    [TestMethod]
    [DataRow("Material", "/Content/Materials/Wood.omat")]
    [DataRow("Geometry", "/Content/Geometry/Cube.ogeo")]
    [DataRow("Scene", "/Content/Scenes/Main.oscene")]
    public async Task BuildIndexAsync_RequiresNativeCookingWithoutWriting(string assetType, string virtualPath)
    {
        var files = new InMemoryImportFileAccess();
        var source = new ImportedAssetSource("source", ReadOnlyMemory<byte>.Empty, DateTimeOffset.UnixEpoch);
        ImportedAsset[] assets = [new(new AssetKey(1, 2), virtualPath, assetType, source, [])];
        var build = new LooseCookedBuildService(fileAccessFactory: _ => files);
        Func<Task> action = () => build.BuildIndexAsync("C:/Fake", assets, CancellationToken.None);
        _ = await action.Should().ThrowAsync<NotSupportedException>().WithMessage("*native content pipeline*").ConfigureAwait(false);
        _ = files.WriteCount.Should().Be(0);
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
            this.WriteCount++;
            this.files[relativePath] = new Entry(bytes.ToArray(), DateTimeOffset.UtcNow);
            return ValueTask.CompletedTask;
        }

        private sealed record Entry(byte[] Bytes, DateTimeOffset LastWriteTimeUtc);
    }
}
