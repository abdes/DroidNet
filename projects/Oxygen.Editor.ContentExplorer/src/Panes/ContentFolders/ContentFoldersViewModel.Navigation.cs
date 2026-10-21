// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Storage;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>Resolves physical folder requests through the browser's declared mount identities.</summary>
public partial class ContentFoldersViewModel
{
    /// <summary>Selects a folder that another pane asks to show, logging the failure to resolve it.</summary>
    /// <param name="folder">The folder.</param>
    /// <returns>A task that completes once the folder is selected or the failure logged.</returns>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A request from another pane ends here; the failure is logged and the tree keeps its selection.")]
    internal async Task ShowFolderAsync(IFolder folder)
    {
        try
        {
            await this.NavigateToFolderAsync(folder).ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            this.LogFolderNavigationFailure(ex);
        }
    }

    // The selection that shows a folder: none for the project, which browses every folder, the folder's virtual path
    // otherwise, or null when no mount shows it.
    private async Task<IReadOnlyList<AssetPath>?> ResolveFolderNavigationAsync(ProjectRootTreeItemAdapter root, IFolder folder)
    {
        var project = session.Context;
        var location = FileSystemPath.From(folder.Location);
        if (location == project.ProjectRoot)
        {
            return [];
        }

        var children = await root.Children.ConfigureAwait(true);
        var cooked = children.OfType<VirtualFolderMountTreeItemAdapter>().FirstOrDefault(static mount => mount.CookedVirtualPaths is not null);
        if (cooked is not null)
        {
            using var selected = await publications.AcquireReadAsync(project, CancellationToken.None).ConfigureAwait(true);
            foreach (var (name, path) in selected.ProjectRoots.Where(pair => location.IsSameOrInside(pair.Value)))
            {
                var below = location.GetPathBelow(path);
                if (cooked.VirtualRootPath.TryCombine(below.Length == 0 ? name : name + "/" + below, out var logical)
                    && await this.FindAdapterByVirtualPathAsync(logical).ConfigureAwait(true) is not null)
                {
                    return [logical];
                }

                return [cooked.VirtualRootPath];
            }
        }

        var mounts = children.OfType<TreeItemAdapter>().Select(static child => child switch
        {
            AuthoringMountPointTreeItemAdapter authoring => (Root: authoring.RootFolder, Path: authoring.VirtualRootPath),
            VirtualFolderMountTreeItemAdapter { CookedVirtualPaths: null } mounted => (Root: mounted.RootFolder, Path: mounted.VirtualRootPath),
            _ => (Root: (IFolder?)null, Path: (AssetPath?)null),
        }).Where(static mount => mount.Root is not null)
            .Select(static mount => (Root: FileSystemPath.From(mount.Root!.Location), Path: mount.Path!))
            .OrderByDescending(static mount => mount.Root.FullPath.Length);
        foreach (var (mountRoot, mountPath) in mounts.Where(mount => location.IsSameOrInside(mount.Root)))
        {
            var below = location.GetPathBelow(mountRoot);
            var path = below.Length == 0 ? mountPath : mountPath.TryCombine(below, out var inside) ? inside : null;
            if (path is not null && await this.FindAdapterByVirtualPathAsync(path).ConfigureAwait(true) is not null)
            {
                return [path];
            }
        }

        return null;
    }
}
