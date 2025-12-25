// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Catalog;
using Oxygen.Storage;

namespace Oxygen.Editor.ContentBrowser.Infrastructure.Assets;

/// <summary>
/// A catalog that aggregates the project's assets and allows dynamic addition of folders.
/// </summary>
public interface IProjectAssetCatalog : IAssetCatalog
{
    /// <summary>
    /// Initializes the catalog by indexing the project root and mount points.
    /// </summary>
    /// <returns>A task that completes when initialization is done.</returns>
    Task InitializeAsync();

    /// <summary>
    /// Adds a folder to the catalog.
    /// </summary>
    /// <param name="folder">The folder to add.</param>
    /// <param name="mountPoint">The mount point to use for the assets in this folder (e.g. "project", "mount-name").</param>
    /// <returns>A task that completes when the folder has been added.</returns>
    Task AddFolderAsync(IFolder folder, string mountPoint);
}
