// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Catalog.Pak;

/// <summary>
/// Options for <see cref="PakIndexAssetCatalog"/>.
/// </summary>
public sealed record PakIndexAssetCatalogOptions
{
    /// <summary>
    /// Gets the mount point / authority used in canonical asset URIs.
    /// </summary>
    /// <remarks>
    /// For example, for Engine shipping content use <c>Engine</c>, and for project packages use a package name.
    /// </remarks>
    public required string Authority { get; init; }

    /// <summary>
    /// Gets the absolute path to the <c>.pak</c> file.
    /// </summary>
    public required string PakFilePath { get; init; }

    /// <summary>
    /// Gets the filesystem watcher filter.
    /// </summary>
    /// <remarks>
    /// Defaults to the pak filename.
    /// </remarks>
    public string? WatcherFilter { get; init; }
}
