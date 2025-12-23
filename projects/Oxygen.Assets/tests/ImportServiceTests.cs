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
        var json = """
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
        _ = result.Imported.Should().HaveCount(1);
        _ = result.Imported[0].VirtualPath.Should().Be("/Content/Materials/Wood.omat");

        _ = files.TryGet(".cooked/Content/Materials/Wood.omat", out var cooked).Should().BeTrue();
        _ = cooked.Length.Should().Be(256);
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

    private sealed class FixedIdentityPolicy : IAssetIdentityPolicy
    {
        public FixedIdentityPolicy(AssetKey key) => this.Key = key;

        public AssetKey Key { get; }

        public AssetKey GetOrCreateAssetKey(string virtualPath, string assetType)
        {
            _ = virtualPath;
            _ = assetType;
            return this.Key;
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

            if (!this.files.TryGetValue(sourcePath, out var bytes))
            {
                // For probe tests, missing file is OK; treat as empty.
                return ValueTask.FromResult<ReadOnlyMemory<byte>>(ReadOnlyMemory<byte>.Empty);
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
