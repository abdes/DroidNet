// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text.Json;
using Moq;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V2;

namespace Oxygen.Testing;

/// <summary>Models the native inventory protocol for orchestration tests; native tests verify binary payload semantics.</summary>
internal static class NativeInventoryFixture
{
    public static IEngineContentPipelineApi CreateApi()
    {
        var api = new Mock<IEngineContentPipelineApi>(MockBehavior.Strict);
        api.Setup(value => value.ReadInventoryAsync(It.IsAny<string>(), It.IsAny<NativeArtifactLease?>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync((string root, NativeArtifactLease? _, CancellationToken _) => Read(root));
        return api.Object;
    }

    public static void WriteIndex(string root, IEnumerable<CookedAssetEntry> assets, Guid? sourceKey = null)
    {
        var entries = assets.Where(static asset => asset.Kind != ContentCookAssetKind.Texture).Select(asset =>
        {
            var path = asset.DescriptorRelativePath ?? asset.VirtualPath.TrimStart('/');
            var bytes = File.ReadAllBytes(Path.Combine(root, path));
            var key = asset.AssetKey is null ? Guid.CreateVersion7() : Guid.Parse(asset.AssetKey);
            return new AssetEntry(AssetKey.FromBytes(key.ToByteArray(bigEndian: true)), path, asset.VirtualPath,
                asset.Kind switch { ContentCookAssetKind.Material => (byte)1, ContentCookAssetKind.Geometry => (byte)2, ContentCookAssetKind.Scene => (byte)3, _ => (byte)0 },
                checked((ulong)bytes.Length), SHA256.HashData(bytes));
        }).ToArray();
        var descriptors = entries.Select(static entry => entry.DescriptorRelativePath).ToHashSet(StringComparer.Ordinal);
        var files = Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories)
            .Select(path => (Path: path, Relative: Path.GetRelativePath(root, path).Replace('\\', '/')))
            .Where(file => file.Relative is not ("container.index.bin" or ".generation.lock") && !descriptors.Contains(file.Relative))
            .Select(file => { var bytes = File.ReadAllBytes(file.Path); return new FileRecord(FileKind.Auxiliary, file.Relative, checked((ulong)bytes.Length), SHA256.HashData(bytes)); }).ToArray();
        using var index = File.Create(Path.Combine(root, "container.index.bin"));
        LooseCookedIndexFixture.Write(index, new Document(0, IndexFeatures.HasVirtualPaths | IndexFeatures.HasFileRecords, sourceKey ?? Guid.CreateVersion7(), entries, files));
    }

    public static CookedInventoryReport Read(string root, bool forceFailure = false)
        => CookedInventoryReport.Parse(ReadJson(root, forceFailure));

    public static string ReadJson(string root, bool forceFailure = false)
    {
        var indexBytes = File.ReadAllBytes(Path.Combine(root, "container.index.bin"));
        using var stream = new MemoryStream(indexBytes, writable: false);
        var index = LooseCookedIndex.Read(stream);
        var members = index.Assets.Select(static asset => (Path: asset.DescriptorRelativePath, Size: asset.DescriptorSize, Digest: asset.DescriptorSha256, Kind: (FileKind?)null))
            .Concat(index.Files.Select(static file => (Path: file.RelativePath, Size: file.Size, Digest: file.Sha256, Kind: (FileKind?)file.Kind))).ToArray();
        var issues = new List<object>();
        foreach (var member in members)
        {
            var path = Path.Combine(root, member.Path);
            var reason = !File.Exists(path) ? "missing" : (ulong)new FileInfo(path).Length != member.Size ? "size_mismatch"
                : !SHA256.HashData(File.ReadAllBytes(path)).AsSpan().SequenceEqual(member.Digest.Span) ? "digest_mismatch" : null;
            if (reason is not null) { issues.Add(new { relative_path = member.Path, reason }); }
        }
        var expected = members.Select(static member => member.Path).ToHashSet(StringComparer.Ordinal);
        foreach (var path in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories))
        {
            var relative = Path.GetRelativePath(root, path).Replace('\\', '/');
            if (relative is not ("container.index.bin" or ".generation.lock") && !expected.Contains(relative))
            { issues.Add(new { relative_path = relative, reason = "unexpected" }); }
        }
        if (forceFailure) { issues.Add(new { relative_path = "controlled-missing.bin", reason = "missing" }); }
        return JsonSerializer.Serialize(new
        {
            schema_version = 1, source_key = index.SourceGuid, index_size = indexBytes.Length,
            index_sha256 = Convert.ToHexStringLower(SHA256.HashData(indexBytes)),
            files = members.Select(static member => new { relative_path = member.Path, size = member.Size, sha256 = Convert.ToHexStringLower(member.Digest.Span), file_kind = member.Kind }),
            assets = index.Assets.Select(static asset => new { asset_key = asset.AssetKey.ToString(), asset_type = asset.AssetType, virtual_path = asset.VirtualPath, descriptor_relative_path = asset.DescriptorRelativePath }),
            resources = index.Files.Where(static file => file.RelativePath.EndsWith(".otex", StringComparison.Ordinal))
                .Select(file => new { kind = "texture", descriptor_relative_path = file.RelativePath, resource_index = issues.Count == 0 ? (uint?)1 : null }),
            issues,
        });
    }
}
