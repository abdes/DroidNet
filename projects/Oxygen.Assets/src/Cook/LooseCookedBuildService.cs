// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
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
    // TODO: Implement a "Compact" cooked data feature in the editor UI later, to reclaim unreferenced data in the files.
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
    /// <returns>A task that completes when the index has been written.</returns>
    public async Task BuildIndexAsync(
        string projectRoot,
        IReadOnlyList<ImportedAsset> imported,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(projectRoot);
        ArgumentNullException.ThrowIfNull(imported);

        var files = this.fileAccessFactory(projectRoot);
        await BuildIndexAsync(files, imported, cancellationToken).ConfigureAwait(false);
    }

    internal static async Task BuildIndexAsync(
        IImportFileAccess files,
        IReadOnlyList<ImportedAsset> imported,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(files);
        ArgumentNullException.ThrowIfNull(imported);

        cancellationToken.ThrowIfCancellationRequested();

        var indexPath = $"{AssetPipelineConstants.CookedFolderName}/{AssetPipelineConstants.IndexFileName}";
        var existingAssets = new Dictionary<AssetKey, AssetEntry>();
        var existingFiles = new Dictionary<string, FileRecord>(StringComparer.Ordinal);

        try
        {
            var existingBytes = await files.ReadAllBytesAsync(indexPath, cancellationToken).ConfigureAwait(false);
            if (existingBytes.Length > 0)
            {
                using var ms = new MemoryStream(existingBytes.ToArray());
                var existingDoc = LooseCookedIndex.Read(ms);

                foreach (var asset in existingDoc.Assets)
                {
                    existingAssets[asset.AssetKey] = asset;
                }

                foreach (var file in existingDoc.Files)
                {
                    existingFiles[file.RelativePath] = file;
                }
            }
        }
        catch (FileNotFoundException)
        {
            // Index doesn't exist yet.
        }

        // Group assets by mount point to organize cooked files on disk.
        var groups = imported
            .GroupBy(static a => GetMountPointFromVirtualPath(a.VirtualPath), StringComparer.Ordinal)
            .OrderBy(static g => g.Key, StringComparer.Ordinal);

        var newFileRecords = new List<FileRecord>();

        foreach (var group in groups)
        {
            cancellationToken.ThrowIfCancellationRequested();

            var mountPoint = group.Key;
            await CookGeometryAsync(files, mountPoint, group, newFileRecords, cancellationToken).ConfigureAwait(false);
            await CookMaterialAsync(files, mountPoint, group, cancellationToken).ConfigureAwait(false);

            foreach (var asset in group.OrderBy(static a => a.VirtualPath, StringComparer.Ordinal))
            {
                cancellationToken.ThrowIfCancellationRequested();

                var descriptorRelativePath = asset.VirtualPath.TrimStart('/');
                var cookedDescriptorPath = AssetPipelineConstants.CookedFolderName + "/" + descriptorRelativePath;

                var bytes = await files.ReadAllBytesAsync(cookedDescriptorPath, cancellationToken).ConfigureAwait(false);
                var sha256 = SHA256.HashData(bytes.Span);

                existingAssets[asset.AssetKey] = new AssetEntry(
                    AssetKey: asset.AssetKey,
                    DescriptorRelativePath: descriptorRelativePath,
                    VirtualPath: asset.VirtualPath,
                    AssetType: MapAssetType(asset.AssetType),
                    DescriptorSize: (ulong)bytes.Length,
                    DescriptorSha256: sha256);
            }
        }

        foreach (var file in newFileRecords)
        {
            existingFiles[file.RelativePath] = file;
        }

        var document = new Document(
            ContentVersion: ContentVersion,
            Flags: IndexFeatures.None,
            Assets: existingAssets.Values.OrderBy(static a => a.VirtualPath, StringComparer.Ordinal).ToList(),
            Files: existingFiles.Values.OrderBy(static f => f.RelativePath, StringComparer.Ordinal).ToList());

        byte[] indexBytes;
        var msOut = new MemoryStream();
        await using (msOut.ConfigureAwait(false))
        {
            LooseCookedIndex.Write(msOut, document);
            indexBytes = msOut.ToArray();
        }

        System.Diagnostics.Debug.WriteLine($"[LooseCookedBuildService] Writing index with {document.Assets.Count} assets to {indexPath}");

        await files
            .WriteAllBytesAsync(indexPath, indexBytes, cancellationToken)
            .ConfigureAwait(false);
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

            var descriptorRelativePath = asset.VirtualPath.TrimStart('/');
            var cookedDescriptorPath = AssetPipelineConstants.CookedFolderName + "/" + descriptorRelativePath;

            byte[] cookedBytes;
            var ms = new MemoryStream();
            await using (ms.ConfigureAwait(false))
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

        // Prepare paths
        const string buffersTableRel = "resources/buffers.table";
        const string buffersDataRel = "resources/buffers.data";
        var tablePath = $"{AssetPipelineConstants.CookedFolderName}/{buffersTableRel}";
        var dataPath = $"{AssetPipelineConstants.CookedFolderName}/{buffersDataRel}";

        // Read existing buffers if they exist
        byte[] existingTable = [];
        byte[] existingData = [];
        try
        {
            var t = await files.ReadAllBytesAsync(tablePath, cancellationToken).ConfigureAwait(false);
            existingTable = t.ToArray();
            var d = await files.ReadAllBytesAsync(dataPath, cancellationToken).ConfigureAwait(false);
            existingData = d.ToArray();
        }
        catch (FileNotFoundException)
        {
            // Ignore
        }

        var cooked = CookBuffers();

        // Merge logic
        if (existingTable.Length > 0)
        {
            // Calculate padding for data alignment (72 bytes)
            var padding = (72 - (existingData.Length % 72)) % 72;
            var baseDataOffset = (ulong)(existingData.Length + padding);
            var baseIndex = (uint)(existingTable.Length / 32); // 32 bytes per desc

            // Strip the first reserved entry (32 bytes) from the new table
            var newTableSpan = cooked.BuffersTableBytes.Span[32..];
            var patchedTable = new byte[newTableSpan.Length];
            newTableSpan.CopyTo(patchedTable);

            // Patch data offsets in the new table
            for (var i = 0; i < patchedTable.Length; i += 32)
            {
                var entry = patchedTable.AsSpan(i, 32);
                var offset = BinaryPrimitives.ReadUInt64LittleEndian(entry[..8]);
                offset += baseDataOffset;
                BinaryPrimitives.WriteUInt64LittleEndian(entry[..8], offset);
            }

            // Adjust indices
            var newIndices = new Dictionary<AssetKey, GeometryBufferPair>();
            foreach (var (key, pair) in cooked.BufferIndices)
            {
                // The pair indices are 1-based relative to the new batch (0 is reserved).
                // We stripped the reserved entry, so index 1 becomes baseIndex.
                // Global = Base + Local - 1
                newIndices[key] = new GeometryBufferPair(
                    VertexBufferIndex: baseIndex + pair.VertexBufferIndex - 1,
                    IndexBufferIndex: baseIndex + pair.IndexBufferIndex - 1);
            }

            // Combine data
            var combinedTable = new byte[existingTable.Length + patchedTable.Length];
            existingTable.CopyTo(combinedTable, 0);
            patchedTable.CopyTo(combinedTable, existingTable.Length);

            var combinedData = new byte[existingData.Length + padding + cooked.BuffersDataBytes.Length];
            existingData.CopyTo(combinedData, 0);
            // Padding is zero-init
            cooked.BuffersDataBytes.CopyTo(combinedData.AsMemory(existingData.Length + padding));

            cooked = new CookedBuffersResult(combinedTable, combinedData, newIndices);
        }

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

                var descriptorRelativePath = asset.VirtualPath.TrimStart('/');
                var cookedDescriptorPath = AssetPipelineConstants.CookedFolderName + "/" + descriptorRelativePath;

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
