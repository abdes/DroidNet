// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Assets.Import;
using Oxygen.Assets.Import.Materials;

namespace Oxygen.Assets.Tests;

[TestClass]
public sealed class SidecarAssetIdentityPolicyTests
{
    [TestMethod]
    public async Task ImportAsync_ShouldReuseAssetKeyFromSidecarAcrossReimport()
    {
        const string sourcePath = "Content/Materials/Wood.omat.json";
        const string sidecarPath = "Content/Materials/Wood.omat.json.import.json";

        var jsonV1 = """
        {
          "Schema": "oxygen.material.v1",
          "Type": "PBR",
          "Name": "Wood"
        }
        """;

        var jsonV2 = """
        {
          "Schema": "oxygen.material.v1",
          "Type": "PBR",
          "Name": "Wood",
          "DoubleSided": true
        }
        """;

        var files = new InMemoryImportFileAccess();
        files.AddUtf8(sourcePath, jsonV1);

        var registry = new ImporterRegistry();
        registry.Register(new MaterialSourceImporter());

        var service = new ImportService(
            registry,
            fileAccessFactory: _ => files,
            identityPolicyFactory: static (f, input, importer, options) => new SidecarAssetIdentityPolicy(f, input, importer, options));

        var request = new ImportRequest(
            ProjectRoot: "C:/Fake",
            Inputs: [new ImportInput(SourcePath: sourcePath, MountPoint: "Content")],
            Options: new ImportOptions(FailFast: true));

        var first = await service.ImportAsync(request, CancellationToken.None).ConfigureAwait(false);
        _ = first.Succeeded.Should().BeTrue();
        _ = files.TryGet(sidecarPath, out var sidecarBytes).Should().BeTrue();
        _ = sidecarBytes.Length.Should().BeGreaterThan(0);

        using (var doc = JsonDocument.Parse(sidecarBytes))
        {
            var root = doc.RootElement;
            _ = root.GetProperty("Importer").GetProperty("Name").GetString().Should().Be("Oxygen.Import.MaterialSource");
            _ = root.GetProperty("Importer").GetProperty("Type").GetString().Should().Be("Oxygen.Assets.Import.Materials.MaterialSourceImporter");
            _ = root.GetProperty("Importer").GetProperty("Version").GetString().Should().NotBeNullOrWhiteSpace();
            _ = root.GetProperty("Importer").GetProperty("Settings").TryGetProperty("FailFast", out _).Should().BeTrue();
        }

        var firstKey = first.Imported.Single().AssetKey;

        files.AddUtf8(sourcePath, jsonV2);

        var second = await service.ImportAsync(request, CancellationToken.None).ConfigureAwait(false);
        _ = second.Succeeded.Should().BeTrue();
        _ = second.Imported.Single().AssetKey.Should().Be(firstKey);
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
