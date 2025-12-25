// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Catalog.FileSystem;

/// <summary>
/// Options for <see cref="FileSystemAssetCatalog"/>.
/// </summary>
public sealed record FileSystemAssetCatalogOptions
{
    /// <summary>
    /// Gets the mount point used in asset URIs.
    /// </summary>
    /// <remarks>
    /// Defaults to <c>Content</c>.
    /// </remarks>
    public string MountPoint { get; init; } = "Content";

    /// <summary>
    /// Gets the root folder path to index.
    /// </summary>
    public required string RootFolderPath { get; init; }

    /// <summary>
    /// Gets the file search pattern.
    /// </summary>
    /// <remarks>
    /// This applies to the underlying filesystem watcher. Snapshot enumeration still uses the storage provider.
    /// </remarks>
    public string WatcherFilter { get; init; } = "*";
}
