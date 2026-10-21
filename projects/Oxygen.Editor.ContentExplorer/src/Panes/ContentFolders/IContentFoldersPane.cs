// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>The explorer's Content Folders pane, as the assets pane asks it to show or create a folder.</summary>
public interface IContentFoldersPane
{
    /// <summary>Selects a folder in the tree, which shows its assets.</summary>
    /// <param name="folder">The folder.</param>
    /// <returns>A task that completes once the folder is selected.</returns>
    public Task ShowFolderAsync(IFolder folder);

    /// <summary>Creates a folder in a writable folder and starts naming it in place.</summary>
    /// <param name="parentFolder">The folder to create it in.</param>
    /// <returns>A task that completes once the folder is created and its name editor opened, or the failure shown.</returns>
    public Task CreateFolderAsync(AssetPath parentFolder);
}
