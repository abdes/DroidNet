// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using Oxygen.Assets.Import;
using Oxygen.Assets.Import.Geometry;
using Oxygen.Assets.Import.Materials;
using Oxygen.Assets.Persistence.LooseCooked.V1;

namespace Oxygen.Assets.Cook;

/// <summary>
/// Minimal build step for loose cooked outputs.
/// </summary>
/// <remarks>
/// MVP responsibility: write <c>container.index.bin</c> for each mount point based on descriptor files
/// that already exist under <c>.cooked/&lt;MountPoint&gt;/</c>.
/// </remarks>
public sealed class LooseCookedBuildService
{
    private const ushort ContentVersion = 1;

    private readonly Func<string, IImportFileAccess> fileAccessFactory;

    /// <summary>
    /// Initializes a new instance of the <see cref="LooseCookedBuildService"/> class.
    /// </summary>
    public LooseCookedBuildService()
        : this(static projectRoot => new SystemIoImportFileAccess(projectRoot))
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="LooseCookedBuildService"/> class.
    /// </summary>
    /// <param name="fileAccessFactory">Factory creating file access bound to the project root.</param>
    public LooseCookedBuildService(Func<string, IImportFileAccess> fileAccessFactory)
    {
        ArgumentNullException.ThrowIfNull(fileAccessFactory);
        this.fileAccessFactory = fileAccessFactory;
    }

    /// <summary>
    /// Writes <c>container.index.bin</c> for each mount point present in <paramref name="imported"/>.
    /// </summary>
    /// <param name="projectRoot">Absolute project root path.</param>
    /// <param name="imported">Imported assets that have corresponding cooked descriptors written.</param>
    /// <param name="cancellationToken">Cancellation token.</param>
    /// <returns>A task that completes when the indexes have been written.</returns>
    public async Task BuildIndexesAsync(
        string projectRoot,
        IReadOnlyList<ImportedAsset> imported,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(projectRoot);
        ArgumentNullException.ThrowIfNull(imported);

        var files = this.fileAccessFactory(projectRoot);
        await BuildIndexesAsync(files, imported, cancellationToken).ConfigureAwait(false);
    }

    internal static async Task BuildIndexesAsync(
        IImportFileAccess files,
        IReadOnlyList<ImportedAsset> imported,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(files);
        ArgumentNullException.ThrowIfNull(imported);

        cancellationToken.ThrowIfCancellationRequested();

        var groups = imported
            .GroupBy(static a => GetMountPointFromVirtualPath(a.VirtualPath), StringComparer.Ordinal)
            .OrderBy(static g => g.Key, StringComparer.Ordinal);

        foreach (var group in groups)
        {
            await BuildIndexForMountPointAsync(group).ConfigureAwait(false);
        }

        async Task BuildIndexForMountPointAsync(IGrouping<string, ImportedAsset> group)
        {
            cancellationToken.ThrowIfCancellationRequested();

            var mountPoint = group.Key;
            var fileRecords = new List<FileRecord>();

            await CookGeometryAsync(files, mountPoint, group, fileRecords, cancellationToken).ConfigureAwait(false);
            await CookMaterialAsync(files, mountPoint, group, cancellationToken).ConfigureAwait(false);
            var entries = new List<AssetEntry>();

            foreach (var asset in group.OrderBy(static a => a.VirtualPath, StringComparer.Ordinal))
            {
                cancellationToken.ThrowIfCancellationRequested();

                var descriptorRelativePath = GetDescriptorRelativePath(asset.VirtualPath, mountPoint);
                var cookedDescriptorPath = ".cooked/" + mountPoint + "/" + descriptorRelativePath;

                var bytes = await files.ReadAllBytesAsync(cookedDescriptorPath, cancellationToken).ConfigureAwait(false);
                var sha256 = SHA256.HashData(bytes.Span);

                entries.Add(
                    new AssetEntry(
                        AssetKey: asset.AssetKey,
                        DescriptorRelativePath: descriptorRelativePath,
                        VirtualPath: asset.VirtualPath,
                        AssetType: MapAssetType(asset.AssetType),
                        DescriptorSize: (ulong)bytes.Length,
                        DescriptorSha256: sha256));
            }

            var document = new Document(
                ContentVersion: ContentVersion,
                Flags: IndexFeatures.None,
                Assets: entries,
                Files: fileRecords);

            byte[] indexBytes;
            var ms = new MemoryStream();
            await using (ms.ConfigureAwait(false))
            {
                LooseCookedIndex.Write(ms, document);
                indexBytes = ms.ToArray();
            }

            await files
                .WriteAllBytesAsync($".cooked/{mountPoint}/container.index.bin", indexBytes, cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private static async Task CookMaterialAsync(
        IImportFileAccess files,
        string mountPoint,
        IGrouping<string, ImportedAsset> group,
        CancellationToken cancellationToken)
    {
        var materials = group
            .Where(static a => string.Equals(a.AssetType, "Material", StringComparison.Ordinal) && a.Payload is MaterialSource)
            .Select(static a => (a, (MaterialSource)a.Payload))
            .ToList();

        if (materials.Count == 0)
        {
            return;
        }

        foreach (var (asset, source) in materials)
        {
            cancellationToken.ThrowIfCancellationRequested();

            var descriptorRelativePath = GetDescriptorRelativePath(asset.VirtualPath, mountPoint);
            var cookedDescriptorPath = ".cooked/" + mountPoint + "/" + descriptorRelativePath;

            byte[] cookedBytes;
            using (var ms = new MemoryStream())
            {
                CookedMaterialWriter.Write(ms, source);
                cookedBytes = ms.ToArray();
            }

            await files.WriteAllBytesAsync(cookedDescriptorPath, cookedBytes, cancellationToken).ConfigureAwait(false);
        }
    }

    private static async Task CookGeometryAsync(
        IImportFileAccess files,
        string mountPoint,
        IGrouping<string, ImportedAsset> group,
        List<FileRecord> fileRecords,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(files);
        ArgumentException.ThrowIfNullOrWhiteSpace(mountPoint);
        ArgumentNullException.ThrowIfNull(group);
        ArgumentNullException.ThrowIfNull(fileRecords);

        var geometry = CollectGeometryAssets();
        if (geometry.Count == 0)
        {
            return;
        }

        var cooked = CookBuffers();
        await WriteBuffersAsync(cooked).ConfigureAwait(false);
        await WriteGeometryDescriptorsAsync(cooked).ConfigureAwait(false);

        List<(ImportedAsset asset, ImportedGeometry payload)> CollectGeometryAssets()
            => [.. group
                .Where(static a => string.Equals(a.AssetType, "Geometry", StringComparison.Ordinal) && a.Payload is ImportedGeometry)
                .Select(static a => (a, (ImportedGeometry)a.Payload))
                .OrderBy(static x => x.a.VirtualPath, StringComparer.Ordinal)
                .Select(static x => (x.a, x.Item2)),];

        CookedBuffersResult CookBuffers()
            => CookedBuffersWriter.Write(
                geometry.ConvertAll(static g => new GeometryCookInput(g.asset.AssetKey, g.payload)));

        async Task WriteBuffersAsync(CookedBuffersResult cooked)
        {
            // Write resources/buffers.*
            const string buffersTableRel = "resources/buffers.table";
            const string buffersDataRel = "resources/buffers.data";

            var tablePath = $".cooked/{mountPoint}/{buffersTableRel}";
            var dataPath = $".cooked/{mountPoint}/{buffersDataRel}";

            await files.WriteAllBytesAsync(tablePath, cooked.BuffersTableBytes, cancellationToken).ConfigureAwait(false);
            await files.WriteAllBytesAsync(dataPath, cooked.BuffersDataBytes, cancellationToken).ConfigureAwait(false);

            fileRecords.Add(CreateFileRecord(FileKind.BuffersTable, buffersTableRel, cooked.BuffersTableBytes));
            fileRecords.Add(CreateFileRecord(FileKind.BuffersData, buffersDataRel, cooked.BuffersDataBytes));
        }

        async Task WriteGeometryDescriptorsAsync(CookedBuffersResult cooked)
        {
            // Write per-asset .ogeo descriptors.
            foreach (var (asset, payload) in geometry)
            {
                cancellationToken.ThrowIfCancellationRequested();

                if (!cooked.BufferIndices.TryGetValue(asset.AssetKey, out var buffers))
                {
                    continue;
                }

                var descriptorRelativePath = GetDescriptorRelativePath(asset.VirtualPath, mountPoint);
                var cookedDescriptorPath = ".cooked/" + mountPoint + "/" + descriptorRelativePath;

                byte[] ogBytes;
                var ms = new MemoryStream();
                await using (ms.ConfigureAwait(false))
                {
                    CookedGeometryWriter.Write(ms, payload, buffers.VertexBufferIndex, buffers.IndexBufferIndex);
                    ogBytes = ms.ToArray();
                }

                await files.WriteAllBytesAsync(cookedDescriptorPath, ogBytes, cancellationToken).ConfigureAwait(false);
            }
        }

        static FileRecord CreateFileRecord(FileKind kind, string relativePath, ReadOnlyMemory<byte> bytes)
            => new(
                Kind: kind,
                RelativePath: relativePath,
                Size: (ulong)bytes.Length,
                Sha256: SHA256.HashData(bytes.Span));
    }

    private static string GetMountPointFromVirtualPath(string virtualPath)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(virtualPath);
        if (!virtualPath.StartsWith('/'))
        {
            throw new InvalidDataException($"VirtualPath must start with '/': '{virtualPath}'.");
        }

        var slash = virtualPath.IndexOf('/', startIndex: 1);
        return slash < 0
            ? throw new InvalidDataException($"VirtualPath must include mount point and file path: '{virtualPath}'.")
            : virtualPath[1..slash];
    }

    private static string GetDescriptorRelativePath(string virtualPath, string mountPoint)
    {
        // /Content/Materials/Wood.omat => Materials/Wood.omat
        var prefix = "/" + mountPoint + "/";
        if (!virtualPath.StartsWith(prefix, StringComparison.Ordinal))
        {
            throw new InvalidDataException($"VirtualPath '{virtualPath}' does not belong to mount point '{mountPoint}'.");
        }

        var relative = virtualPath[prefix.Length..];
        return string.IsNullOrWhiteSpace(relative)
            ? throw new InvalidDataException($"VirtualPath '{virtualPath}' does not include a descriptor path.")
            : relative;
    }

    private static byte MapAssetType(string assetType)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(assetType);

        // Matches runtime oxygen::data::AssetType values.
        return assetType switch
        {
            "Material" => 1,
            "Geometry" => 2,
            "Scene" => 3,
            _ => 0,
        };
    }
}
