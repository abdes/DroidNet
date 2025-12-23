// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using Oxygen.Assets.Import;
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
            cancellationToken.ThrowIfCancellationRequested();

            var mountPoint = group.Key;
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
                Files: Array.Empty<FileRecord>());

            byte[] indexBytes;
            using (var ms = new MemoryStream())
            {
                LooseCookedIndex.Write(ms, document);
                indexBytes = ms.ToArray();
            }

            await files
                .WriteAllBytesAsync($".cooked/{mountPoint}/container.index.bin", indexBytes, cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private static string GetMountPointFromVirtualPath(string virtualPath)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(virtualPath);
        if (!virtualPath.StartsWith('/'))
        {
            throw new InvalidDataException($"VirtualPath must start with '/': '{virtualPath}'.");
        }

        var slash = virtualPath.IndexOf('/', startIndex: 1);
        if (slash < 0)
        {
            throw new InvalidDataException($"VirtualPath must include mount point and file path: '{virtualPath}'.");
        }

        return virtualPath[1..slash];
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
        if (string.IsNullOrWhiteSpace(relative))
        {
            throw new InvalidDataException($"VirtualPath '{virtualPath}' does not include a descriptor path.");
        }

        return relative;
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
