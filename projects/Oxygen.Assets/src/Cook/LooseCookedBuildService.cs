// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Security.Cryptography;
using Oxygen.Assets.Import;
using Oxygen.Assets.Persistence.LooseCooked.V1;
using SixLabors.ImageSharp;
using SixLabors.ImageSharp.PixelFormats;

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

        var groups = imported
            .GroupBy(static a => GetMountPointFromVirtualPath(a.VirtualPath), StringComparer.Ordinal)
            .OrderBy(static g => g.Key, StringComparer.Ordinal);

        var loader = new IntermediateAssetLoader(files);

        foreach (var group in groups)
        {
            cancellationToken.ThrowIfCancellationRequested();
            await BuildSingleMountIndexAsync(files, loader, group.Key, group, cancellationToken).ConfigureAwait(false);
        }
    }

    internal static async Task RepairIndexFileRecordsAsync(
        IImportFileAccess files,
        IEnumerable<string> mountPoints,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(files);
        ArgumentNullException.ThrowIfNull(mountPoints);

        foreach (var mountPoint in mountPoints.Distinct(StringComparer.Ordinal).OrderBy(static x => x, StringComparer.Ordinal))
        {
            cancellationToken.ThrowIfCancellationRequested();

            await RepairSingleMountIndexFileRecordsAsync(files, mountPoint, cancellationToken).ConfigureAwait(false);
        }
    }

    private static async Task RepairSingleMountIndexFileRecordsAsync(
        IImportFileAccess files,
        string mountPoint,
        CancellationToken cancellationToken)
    {
        var indexPath = $"{AssetPipelineConstants.CookedFolderName}/{mountPoint}/{AssetPipelineConstants.IndexFileName}";
        byte[] indexBytes;
        try
        {
            indexBytes = (await files.ReadAllBytesAsync(indexPath, cancellationToken).ConfigureAwait(false)).ToArray();
        }
        catch (FileNotFoundException)
        {
            return;
        }

        if (indexBytes.Length == 0)
        {
            return;
        }

        Document doc;
        using (var ms = new MemoryStream(indexBytes, writable: false))
        {
            doc = LooseCookedIndex.Read(ms);
        }

        var fileRecords = doc.Files.ToDictionary(static f => f.RelativePath, StringComparer.Ordinal);
        var known = GetKnownResourceFileRecords();
        foreach (var (kind, relativePath) in known)
        {
            cancellationToken.ThrowIfCancellationRequested();
            await TryUpdateFileRecordFromDiskAsync(files, mountPoint, kind, relativePath, fileRecords, cancellationToken)
                .ConfigureAwait(false);
        }

        var updatedDoc = new Document(
            ContentVersion: doc.ContentVersion,
            Flags: doc.Flags,
            SourceGuid: doc.SourceGuid == Guid.Empty ? Guid.NewGuid() : doc.SourceGuid,
            Assets: doc.Assets,
            Files: fileRecords.Values.OrderBy(static f => f.RelativePath, StringComparer.Ordinal).ToList());

        byte[] outBytes;
        using (var msOut = new MemoryStream())
        {
            LooseCookedIndex.Write(msOut, updatedDoc);
            outBytes = msOut.ToArray();
        }

        await files.WriteAllBytesAsync(indexPath, outBytes, cancellationToken).ConfigureAwait(false);
    }

    private static (FileKind kind, string relativePath)[] GetKnownResourceFileRecords()
        =>
        [
            (FileKind.BuffersTable, "resources/buffers.table"),
            (FileKind.BuffersData, "resources/buffers.data"),
            (FileKind.TexturesTable, "resources/textures.table"),
            (FileKind.TexturesData, "resources/textures.data"),
        ];

    private static async Task TryUpdateFileRecordFromDiskAsync(
        IImportFileAccess files,
        string mountPoint,
        FileKind kind,
        string relativePath,
        Dictionary<string, FileRecord> fileRecords,
        CancellationToken cancellationToken)
    {
        var cookedPath = $"{AssetPipelineConstants.CookedFolderName}/{mountPoint}/{relativePath}";
        ReadOnlyMemory<byte> bytes;
        try
        {
            bytes = await files.ReadAllBytesAsync(cookedPath, cancellationToken).ConfigureAwait(false);
        }
        catch (FileNotFoundException)
        {
            return;
        }

        fileRecords[relativePath] = new FileRecord(
            Kind: kind,
            RelativePath: relativePath,
            Size: (ulong)bytes.Length,
            Sha256: SHA256.HashData(bytes.Span));
    }

    private static async Task BuildSingleMountIndexAsync(
        IImportFileAccess files,
        IntermediateAssetLoader loader,
        string mountPoint,
        IGrouping<string, ImportedAsset> group,
        CancellationToken cancellationToken)
    {
        var indexPath = $"{AssetPipelineConstants.CookedFolderName}/{mountPoint}/{AssetPipelineConstants.IndexFileName}";
        var (existingSourceGuid, existingAssets, existingFiles) = await TryLoadExistingIndexAsync(files, indexPath, cancellationToken).ConfigureAwait(false);

        var updatedAssetKeys = new HashSet<AssetKey>();
        var virtualPathToKey = BuildVirtualPathToKeyMap(existingAssets);

        var newFileRecords = new List<FileRecord>();

        // Best-effort: never leave the index stale if we already updated any cooked files.
        // This avoids runtime mount failures due to file record size/SHA mismatches.
        await RunBestEffortAsync(
                stage: "CookGeometry",
                mountPoint,
                () => CookGeometryAsync(files, loader, mountPoint, group, newFileRecords, cancellationToken))
            .ConfigureAwait(false);

        await RunBestEffortAsync(
                stage: "CookMaterial",
                mountPoint,
                () => CookMaterialAsync(files, loader, group, cancellationToken))
            .ConfigureAwait(false);

        await RunBestEffortAsync(
                stage: "CookTexture",
                mountPoint,
                () => CookTextureAsync(files, loader, mountPoint, group, newFileRecords, cancellationToken))
            .ConfigureAwait(false);

        await RunBestEffortAsync(
                stage: "CookScene",
                mountPoint,
                () => CookSceneAsync(files, loader, group, cancellationToken))
            .ConfigureAwait(false);

        await RunBestEffortAsync(
                stage: "UpdateAssetEntries",
                mountPoint,
                () => UpdateAssetEntriesAsync(files, mountPoint, group, existingAssets, virtualPathToKey, updatedAssetKeys, cancellationToken))
            .ConfigureAwait(false);

        UpdateFileEntries(newFileRecords, existingFiles);

        var dedupedAssets = DeduplicateAssetsByVirtualPath(existingAssets.Values, updatedAssetKeys);

        var sourceGuid = existingSourceGuid == Guid.Empty ? Guid.NewGuid() : existingSourceGuid;

        var document = new Document(
            ContentVersion: ContentVersion,
            Flags: IndexFeatures.None,
            SourceGuid: sourceGuid,
            Assets: dedupedAssets,
            Files: existingFiles.Values.OrderBy(static f => f.RelativePath, StringComparer.Ordinal).ToList());

        await WriteIndexAsync(files, indexPath, document, cancellationToken).ConfigureAwait(false);
    }

    private static async Task RunBestEffortAsync(string stage, string mountPoint, Func<Task> action)
    {
        try
        {
            await action().ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex) when (ex is InvalidDataException
            or IOException
            or UnauthorizedAccessException
            or System.Text.Json.JsonException
            or NotSupportedException
            or ArgumentException
            or FormatException)
        {
            System.Diagnostics.Debug.WriteLine(
                $"[LooseCookedBuildService] {stage} failed for mount '{mountPoint}': {ex.Message}");
        }
    }

    private static async Task<(Guid sourceGuid, Dictionary<AssetKey, AssetEntry> assets, Dictionary<string, FileRecord> files)>
        TryLoadExistingIndexAsync(IImportFileAccess files, string indexPath, CancellationToken cancellationToken)
    {
        var existingSourceGuid = Guid.Empty;
        var existingAssets = new Dictionary<AssetKey, AssetEntry>();
        var existingFiles = new Dictionary<string, FileRecord>(StringComparer.Ordinal);

        try
        {
            var existingBytes = await files.ReadAllBytesAsync(indexPath, cancellationToken).ConfigureAwait(false);
            if (existingBytes.Length == 0)
            {
                return (existingSourceGuid, existingAssets, existingFiles);
            }

            using var ms = new MemoryStream(existingBytes.ToArray());
            var existingDoc = LooseCookedIndex.Read(ms);
            existingSourceGuid = existingDoc.SourceGuid;

            foreach (var asset in existingDoc.Assets)
            {
                existingAssets[asset.AssetKey] = asset;
            }

            foreach (var file in existingDoc.Files)
            {
                existingFiles[file.RelativePath] = file;
            }
        }
        catch (FileNotFoundException)
        {
            // Index doesn't exist yet.
        }

        return (existingSourceGuid, existingAssets, existingFiles);
    }

    private static async Task UpdateAssetEntriesAsync(
        IImportFileAccess files,
        string mountPoint,
        IEnumerable<ImportedAsset> group,
        Dictionary<AssetKey, AssetEntry> existingAssets,
        Dictionary<string, AssetKey> virtualPathToKey,
        HashSet<AssetKey> updatedAssetKeys,
        CancellationToken cancellationToken)
    {
        foreach (var asset in group.OrderBy(static a => a.VirtualPath, StringComparer.Ordinal))
        {
            cancellationToken.ThrowIfCancellationRequested();

            var descriptorRelativePath = asset.VirtualPath.TrimStart('/');
            var cookedDescriptorPath = AssetPipelineConstants.CookedFolderName + "/" + descriptorRelativePath;

            ReadOnlyMemory<byte> bytes;
            try
            {
                bytes = await files.ReadAllBytesAsync(cookedDescriptorPath, cancellationToken).ConfigureAwait(false);
            }
            catch (FileNotFoundException)
            {
                // Best-effort build: if a descriptor wasn't produced (or failed to write),
                // drop the entry rather than failing the whole mount and leaving the index stale.
                existingAssets.Remove(asset.AssetKey);
                updatedAssetKeys.Remove(asset.AssetKey);
                continue;
            }

            var sha256 = SHA256.HashData(bytes.Span);

            var entryRelativePath = StripMountPrefix(descriptorRelativePath, mountPoint);

            // Reimport case:
            // The identity policy may produce a new AssetKey for the same VirtualPath.
            // Ensure the index ends up with a single entry per VirtualPath (engine requires this).
            if (virtualPathToKey.TryGetValue(asset.VirtualPath, out var previousKey) && previousKey != asset.AssetKey)
            {
                _ = existingAssets.Remove(previousKey);
                updatedAssetKeys.Remove(previousKey);
            }

            existingAssets[asset.AssetKey] = new AssetEntry(
                AssetKey: asset.AssetKey,
                DescriptorRelativePath: entryRelativePath,
                VirtualPath: asset.VirtualPath,
                AssetType: MapAssetType(asset.AssetType),
                DescriptorSize: (ulong)bytes.Length,
                DescriptorSha256: sha256);

            virtualPathToKey[asset.VirtualPath] = asset.AssetKey;
            updatedAssetKeys.Add(asset.AssetKey);
        }
    }

    private static Dictionary<string, AssetKey> BuildVirtualPathToKeyMap(Dictionary<AssetKey, AssetEntry> existingAssets)
    {
        var map = new Dictionary<string, AssetKey>(StringComparer.Ordinal);
        foreach (var (key, entry) in existingAssets)
        {
            if (!string.IsNullOrEmpty(entry.VirtualPath))
            {
                map[entry.VirtualPath] = key;
            }
        }

        return map;
    }

    private static List<AssetEntry> DeduplicateAssetsByVirtualPath(IEnumerable<AssetEntry> assets, HashSet<AssetKey> preferredKeys)
    {
        var byVirtualPath = new Dictionary<string, AssetEntry>(StringComparer.Ordinal);

        var nonDedupe = new List<AssetEntry>();

        static IOrderedEnumerable<AssetEntry> OrderStable(IEnumerable<AssetEntry> source)
            => source
                .OrderBy(static a => a.VirtualPath, StringComparer.Ordinal)
                .ThenBy(static a => a.AssetKey.Part0)
                .ThenBy(static a => a.AssetKey.Part1);

        var all = assets as ICollection<AssetEntry> ?? assets.ToList();
        foreach (var entry in all)
        {
            if (string.IsNullOrEmpty(entry.VirtualPath))
            {
                nonDedupe.Add(entry);
            }
        }

        var allWithVirtualPath = all.Where(static a => !string.IsNullOrEmpty(a.VirtualPath));

        // First, keep non-preferred entries.
        foreach (var entry in OrderStable(allWithVirtualPath.Where(a => !preferredKeys.Contains(a.AssetKey))))
        {
            byVirtualPath[entry.VirtualPath!] = entry;
        }

        // Then overwrite with preferred (recently updated) entries.
        foreach (var entry in OrderStable(allWithVirtualPath.Where(a => preferredKeys.Contains(a.AssetKey))))
        {
            byVirtualPath[entry.VirtualPath!] = entry;
        }

        if (byVirtualPath.Count + nonDedupe.Count != all.Count)
        {
            System.Diagnostics.Debug.WriteLine(
            $"[LooseCookedBuildService] Deduplicated assets by VirtualPath: {all.Count} -> {byVirtualPath.Count + nonDedupe.Count}");
        }

        var result = byVirtualPath.Values.OrderBy(static a => a.VirtualPath, StringComparer.Ordinal).ToList();
        result.AddRange(nonDedupe);
        return result;
    }

    private static void UpdateFileEntries(IEnumerable<FileRecord> newFileRecords, Dictionary<string, FileRecord> existingFiles)
    {
        foreach (var file in newFileRecords)
        {
            existingFiles[file.RelativePath] = file;
        }
    }

    private static string StripMountPrefix(string descriptorRelativePath, string mountPoint)
    {
        if (descriptorRelativePath.StartsWith(mountPoint + "/", StringComparison.Ordinal))
        {
            return descriptorRelativePath[(mountPoint.Length + 1)..];
        }

        return descriptorRelativePath;
    }

    private static async Task WriteIndexAsync(
        IImportFileAccess files,
        string indexPath,
        Document document,
        CancellationToken cancellationToken)
    {
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
        IntermediateAssetLoader loader,
        IGrouping<string, ImportedAsset> group,
        CancellationToken cancellationToken)
    {
        var materials = group
            .Where(static a => string.Equals(a.AssetType, "Material", StringComparison.Ordinal))
            .ToList();

        if (materials.Count == 0)
        {
            return;
        }

        foreach (var asset in materials)
        {
            cancellationToken.ThrowIfCancellationRequested();

            var source = await loader.LoadMaterialAsync(asset, cancellationToken).ConfigureAwait(false);
            if (source == null)
            {
                continue;
            }

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

    private static async Task CookSceneAsync(
        IImportFileAccess files,
        IntermediateAssetLoader loader,
        IGrouping<string, ImportedAsset> group,
        CancellationToken cancellationToken)
    {
        var scenes = group
            .Where(static a => string.Equals(a.AssetType, "Scene", StringComparison.Ordinal))
            .ToList();

        if (scenes.Count == 0)
        {
            return;
        }

        foreach (var asset in scenes)
        {
            cancellationToken.ThrowIfCancellationRequested();

            var source = await loader.LoadSceneAsync(asset, cancellationToken).ConfigureAwait(false);
            if (source == null)
            {
                continue;
            }

            var descriptorRelativePath = asset.VirtualPath.TrimStart('/');
            var cookedDescriptorPath = AssetPipelineConstants.CookedFolderName + "/" + descriptorRelativePath;

            // MVP: Just write the JSON source as the cooked descriptor for now.
            // In the future, this should use CookedSceneWriter to produce a binary format.
            if (string.IsNullOrEmpty(asset.GeneratedSourcePath))
            {
                continue;
            }

            var jsonBytes = await files.ReadAllBytesAsync(asset.GeneratedSourcePath, cancellationToken).ConfigureAwait(false);

            await files.WriteAllBytesAsync(cookedDescriptorPath, jsonBytes, cancellationToken).ConfigureAwait(false);
        }
    }

    private static async Task CookGeometryAsync(
        IImportFileAccess files,
        IntermediateAssetLoader loader,
        string mountPoint,
        IGrouping<string, ImportedAsset> group,
        List<FileRecord> fileRecords,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(files);
        ArgumentException.ThrowIfNullOrWhiteSpace(mountPoint);
        ArgumentNullException.ThrowIfNull(group);
        ArgumentNullException.ThrowIfNull(fileRecords);

        var geometry = await CollectGeometryAssetsAsync().ConfigureAwait(false);
        if (geometry.Count == 0)
        {
            return;
        }

        // Prepare paths
        const string buffersTableRel = "resources/buffers.table";
        const string buffersDataRel = "resources/buffers.data";
        var tablePath = $"{AssetPipelineConstants.CookedFolderName}/{mountPoint}/{buffersTableRel}";
        var dataPath = $"{AssetPipelineConstants.CookedFolderName}/{mountPoint}/{buffersDataRel}";

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
        cooked = MergeCookedBuffers(existingTable, existingData, cooked);

        await WriteBuffersAsync(cooked).ConfigureAwait(false);
        await WriteGeometryDescriptorsAsync(cooked).ConfigureAwait(false);

        async Task<List<(ImportedAsset asset, GeometryCookInput input)>> CollectGeometryAssetsAsync()
        {
            var list = new List<(ImportedAsset, GeometryCookInput)>();
            foreach (var asset in group.Where(static a => string.Equals(a.AssetType, "Geometry", StringComparison.Ordinal)))
            {
                GeometryCookInput? input = null;
                try
                {
                    input = await loader.LoadGeometryAsync(asset, cancellationToken).ConfigureAwait(false);
                }
                catch (InvalidDataException ex)
                {
                    System.Diagnostics.Debug.WriteLine(
                        $"[LooseCookedBuildService] Failed to load geometry for '{asset.VirtualPath}': {ex.Message}");
                }
                catch (FileNotFoundException ex)
                {
                    System.Diagnostics.Debug.WriteLine(
                        $"[LooseCookedBuildService] Failed to load geometry for '{asset.VirtualPath}': {ex.Message}");
                }
                catch (System.Text.Json.JsonException ex)
                {
                    System.Diagnostics.Debug.WriteLine(
                        $"[LooseCookedBuildService] Failed to load geometry for '{asset.VirtualPath}': {ex.Message}");
                }

                if (input != null)
                {
                    list.Add((asset, input));
                }
            }

            return list.OrderBy(static x => x.Item1.VirtualPath, StringComparer.Ordinal).ToList();
        }

        CookedBuffersResult CookBuffers()
            => CookedBuffersWriter.Write(geometry.ConvertAll(static g => g.input));

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
            foreach (var (asset, input) in geometry)
            {
                cancellationToken.ThrowIfCancellationRequested();

                if (!cooked.BufferIndices.TryGetValue(asset.AssetKey, out var buffers))
                {
                    continue;
                }

                var descriptorRelativePath = asset.VirtualPath.TrimStart('/');
                var cookedDescriptorPath = AssetPipelineConstants.CookedFolderName + "/" + descriptorRelativePath;

                try
                {
                    byte[] ogBytes;
                    var ms = new MemoryStream();
                    await using (ms.ConfigureAwait(false))
                    {
                        CookedGeometryWriter.Write(ms, input.Geometry, buffers.VertexBufferIndex, buffers.IndexBufferIndex);
                        ogBytes = ms.ToArray();
                    }

                    await files.WriteAllBytesAsync(cookedDescriptorPath, ogBytes, cancellationToken).ConfigureAwait(false);
                }
                catch (InvalidDataException ex)
                {
                    System.Diagnostics.Debug.WriteLine(
                        $"[LooseCookedBuildService] Failed to write cooked geometry '{asset.VirtualPath}': {ex.Message}");
                }
                catch (ArgumentException ex)
                {
                    System.Diagnostics.Debug.WriteLine(
                        $"[LooseCookedBuildService] Failed to write cooked geometry '{asset.VirtualPath}': {ex.Message}");
                }
                catch (IOException ex)
                {
                    System.Diagnostics.Debug.WriteLine(
                        $"[LooseCookedBuildService] Failed to write cooked geometry '{asset.VirtualPath}': {ex.Message}");
                }
                catch (UnauthorizedAccessException ex)
                {
                    System.Diagnostics.Debug.WriteLine(
                        $"[LooseCookedBuildService] Failed to write cooked geometry '{asset.VirtualPath}': {ex.Message}");
                }
            }
        }

        static FileRecord CreateFileRecord(FileKind kind, string relativePath, ReadOnlyMemory<byte> bytes)
            => new(
                Kind: kind,
                RelativePath: relativePath,
                Size: (ulong)bytes.Length,
                Sha256: SHA256.HashData(bytes.Span));
    }

    private static CookedBuffersResult MergeCookedBuffers(byte[] existingTable, byte[] existingData, CookedBuffersResult cooked)
    {
        if (existingTable.Length == 0)
        {
            return cooked;
        }

        // Calculate padding for data alignment (72 bytes)
        const int bufferAlignmentBytes = 72;
        const int tableEntrySize = 32;
        var padding = (bufferAlignmentBytes - (existingData.Length % bufferAlignmentBytes)) % bufferAlignmentBytes;
        var baseDataOffset = (ulong)(existingData.Length + padding);
        var baseIndex = (uint)(existingTable.Length / tableEntrySize);

        // Strip the first reserved entry (32 bytes) from the new table
        var newTableSpan = cooked.BuffersTableBytes.Span[tableEntrySize..];
        var patchedTable = new byte[newTableSpan.Length];
        newTableSpan.CopyTo(patchedTable);

        // Patch data offsets in the new table
        for (var i = 0; i < patchedTable.Length; i += tableEntrySize)
        {
            var entry = patchedTable.AsSpan(i, tableEntrySize);
            var offset = BinaryPrimitives.ReadUInt64LittleEndian(entry[..8]);
            offset += baseDataOffset;
            BinaryPrimitives.WriteUInt64LittleEndian(entry[..8], offset);
        }

        // Adjust indices
        var newIndices = new Dictionary<AssetKey, GeometryBufferPair>();
        foreach (var (key, pair) in cooked.BufferIndices)
        {
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

        return new CookedBuffersResult(combinedTable, combinedData, newIndices);
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
            "Texture" => 4,
            _ => 0,
        };
    }

    private static async Task CookTextureAsync(
        IImportFileAccess files,
        IntermediateAssetLoader loader,
        string mountPoint,
        IGrouping<string, ImportedAsset> group,
        List<FileRecord> fileRecords,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(files);
        ArgumentException.ThrowIfNullOrWhiteSpace(mountPoint);
        ArgumentNullException.ThrowIfNull(group);
        ArgumentNullException.ThrowIfNull(fileRecords);

        var textures = await CollectTextureAssetsAsync().ConfigureAwait(false);
        if (textures.Count == 0)
        {
            return;
        }

        // Prepare paths
        const string texturesTableRel = "resources/textures.table";
        const string texturesDataRel = "resources/textures.data";
        var tablePath = $"{AssetPipelineConstants.CookedFolderName}/{mountPoint}/{texturesTableRel}";
        var dataPath = $"{AssetPipelineConstants.CookedFolderName}/{mountPoint}/{texturesDataRel}";

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

        var cooked = CookTextures();
        cooked = MergeCookedTextures(existingTable, existingData, cooked);

        await WriteTexturesAsync(cooked).ConfigureAwait(false);
        await WriteTextureDescriptorsAsync(cooked).ConfigureAwait(false);

        async Task<List<(ImportedAsset asset, TextureCookInput cookInput)>> CollectTextureAssetsAsync()
        {
            var list = new List<(ImportedAsset, TextureCookInput)>();
            foreach (var asset in group.Where(static a => string.Equals(a.AssetType, "Texture", StringComparison.Ordinal)))
            {
                var source = await loader.LoadTextureSourceAsync(asset, cancellationToken).ConfigureAwait(false);
                if (source is null)
                {
                    continue;
                }

                if (string.IsNullOrWhiteSpace(asset.IntermediateCachePath))
                {
                    throw new InvalidDataException($"Texture asset '{asset.VirtualPath}' is missing IntermediateCachePath.");
                }

                var snapshotBytes = await loader.LoadTextureBytesAsync(asset, cancellationToken).ConfigureAwait(false);
                if (snapshotBytes is null)
                {
                    continue;
                }

                var cookedInput = TextureCooker.Cook(asset.AssetKey, source, snapshotBytes);
                list.Add((asset, cookedInput));
            }

            return list.OrderBy(static x => x.Item1.VirtualPath, StringComparer.Ordinal).ToList();
        }

        CookedTexturesResult CookTextures()
            => CookedTexturesWriter.Write(
                textures.ConvertAll(static t => t.cookInput));

        async Task WriteTexturesAsync(CookedTexturesResult cooked)
        {
            await files.WriteAllBytesAsync(tablePath, cooked.TableBytes, cancellationToken).ConfigureAwait(false);
            await files.WriteAllBytesAsync(dataPath, cooked.DataBytes, cancellationToken).ConfigureAwait(false);

            fileRecords.Add(CreateFileRecord(FileKind.TexturesTable, texturesTableRel, cooked.TableBytes));
            fileRecords.Add(CreateFileRecord(FileKind.TexturesData, texturesDataRel, cooked.DataBytes));
        }

        async Task WriteTextureDescriptorsAsync(CookedTexturesResult cooked)
        {
            foreach (var (asset, _) in textures)
            {
                cancellationToken.ThrowIfCancellationRequested();

                if (!cooked.Indices.TryGetValue(asset.AssetKey, out var index))
                {
                    continue;
                }

                var descriptorRelativePath = asset.VirtualPath.TrimStart('/');
                var cookedDescriptorPath = AssetPipelineConstants.CookedFolderName + "/" + descriptorRelativePath;

                byte[] otBytes;
                var ms = new MemoryStream();
                await using (ms.ConfigureAwait(false))
                {
                    CookedTextureWriter.Write(ms, index);
                    otBytes = ms.ToArray();
                }

                await files.WriteAllBytesAsync(cookedDescriptorPath, otBytes, cancellationToken).ConfigureAwait(false);
            }
        }

        static FileRecord CreateFileRecord(FileKind kind, string relativePath, ReadOnlyMemory<byte> bytes)
            => new(
                Kind: kind,
                RelativePath: relativePath,
                Size: (ulong)bytes.Length,
                Sha256: SHA256.HashData(bytes.Span));
    }

    private static CookedTexturesResult MergeCookedTextures(
        byte[] existingTable,
        byte[] existingData,
        CookedTexturesResult cooked)
    {
        if (existingTable.Length == 0)
        {
            return cooked;
        }

        // Textures are always aligned to 256 bytes.
        const int textureAlignmentBytes = 256;
        const int tableEntrySize = 40;
        var padding = (textureAlignmentBytes - (existingData.Length % textureAlignmentBytes)) % textureAlignmentBytes;
        var baseDataOffset = (ulong)(existingData.Length + padding);
        var baseIndex = (uint)(existingTable.Length / tableEntrySize);

        // Strip the first reserved entry (40 bytes) from the new table
        var newTableSpan = cooked.TableBytes.Span[tableEntrySize..];
        var patchedTable = new byte[newTableSpan.Length];
        newTableSpan.CopyTo(patchedTable);

        // Patch data offsets in the new table
        for (var i = 0; i < patchedTable.Length; i += tableEntrySize)
        {
            var entry = patchedTable.AsSpan(i, tableEntrySize);
            var offset = BinaryPrimitives.ReadUInt64LittleEndian(entry[..8]);
            offset += baseDataOffset;
            BinaryPrimitives.WriteUInt64LittleEndian(entry[..8], offset);
        }

        // Adjust indices
        var newIndices = new Dictionary<AssetKey, uint>();
        foreach (var (key, index) in cooked.Indices)
        {
            // Global = Base + Local - 1
            newIndices[key] = baseIndex + index - 1;
        }

        // Combine data
        var combinedTable = new byte[existingTable.Length + patchedTable.Length];
        existingTable.CopyTo(combinedTable, 0);
        patchedTable.CopyTo(combinedTable, existingTable.Length);

        var combinedData = new byte[existingData.Length + padding + cooked.DataBytes.Length];
        existingData.CopyTo(combinedData, 0);

        // Padding is zero-init
        cooked.DataBytes.CopyTo(combinedData.AsMemory(existingData.Length + padding));

        return new CookedTexturesResult(combinedTable, combinedData, newIndices);
    }
}
