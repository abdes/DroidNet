// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>The explorer's Content Folders pane while it lives: the explorer's router creates it, and it attaches.</summary>
/// <remarks>A request while no pane is attached does nothing.</remarks>
public sealed class ContentFoldersPane : IContentFoldersPane
{
    private ContentFoldersViewModel? pane;

    /// <inheritdoc />
    public Task ShowFolderAsync(IFolder folder)
        => this.pane is { } current ? current.ShowFolderAsync(folder) : Task.CompletedTask;

    /// <inheritdoc />
    public Task CreateFolderAsync(AssetPath parentFolder)
        => this.pane is { } current ? current.CreateFolderAsync(parentFolder) : Task.CompletedTask;

    /// <summary>Makes a pane the one that takes the requests.</summary>
    /// <param name="model">The pane, which the explorer's router created.</param>
    internal void Attach(ContentFoldersViewModel model) => this.pane = model;

    /// <summary>Forgets a pane that ended.</summary>
    /// <param name="model">The pane.</param>
    internal void Detach(ContentFoldersViewModel model)
    {
        if (ReferenceEquals(this.pane, model))
        {
            this.pane = null;
        }
    }
}
