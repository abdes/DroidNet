// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Storage;
using Oxygen.Managed.Assets.Filesystem;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>Resolves physical folder requests through the browser's declared mount identities.</summary>
public partial class ProjectLayoutViewModel
{
    private async Task<string?> ResolveFolderNavigationPathAsync(ProjectRootTreeItemAdapter root, IFolder folder)
    {
        if (string.Equals(folder.GetPathRelativeTo(contentBrowserState.ProjectRootPath), ".", StringComparison.Ordinal))
        {
            return "/";
        }

        var children = await root.Children.ConfigureAwait(true);
        var cooked = children.OfType<VirtualFolderMountTreeItemAdapter>().FirstOrDefault(static mount => mount.CookedVirtualPaths is not null);
        if (cooked is not null && projectContextService.ActiveProject is { } project)
        {
            using var selected = await publications.AcquireReadAsync(project, CancellationToken.None).ConfigureAwait(true);
            foreach (var (name, path) in selected.ProjectRoots)
            {
                var relative = folder.GetPathRelativeTo(path).Replace('\\', '/');
                if (Path.IsPathRooted(relative) || relative is ".." || relative.StartsWith("../", StringComparison.Ordinal))
                {
                    continue;
                }

                var logical = VirtualPath.Combine(cooked.VirtualRootPath, relative is "." or "" ? name : name + "/" + relative);
                return await this.FindAdapterByVirtualPathAsync(logical).ConfigureAwait(true) is not null ? logical : cooked.VirtualRootPath;
            }
        }

        var mounts = children.OfType<TreeItemAdapter>().Select(static child => child switch
        {
            AuthoringMountPointTreeItemAdapter authoring => (Root: authoring.RootFolder, Path: authoring.VirtualRootPath),
            VirtualFolderMountTreeItemAdapter { CookedVirtualPaths: null } mounted => (Root: mounted.RootFolder, Path: mounted.VirtualRootPath),
            _ => (Root: (IFolder?)null, Path: (string?)null),
        }).Where(static mount => mount.Root is not null).OrderByDescending(static mount => mount.Root!.Location.Length);
        foreach (var mount in mounts)
        {
            var relative = folder.GetPathRelativeTo(mount.Root!.Location).Replace('\\', '/');
            if (Path.IsPathRooted(relative) || relative is ".." || relative.StartsWith("../", StringComparison.Ordinal))
            {
                continue;
            }

            var path = relative is "." or "" ? mount.Path! : VirtualPath.Combine(mount.Path!, relative);
            if (await this.FindAdapterByVirtualPathAsync(path).ConfigureAwait(true) is not null)
            {
                return path;
            }
        }

        return null;
    }
}
