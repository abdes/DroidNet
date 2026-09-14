// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using System.Reactive.Threading.Tasks;
using DryIoc;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.ProjectExplorer;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentBrowser.Shell;

/// <summary>Handles explicit navigation to asset identities without changing cook state.</summary>
public sealed partial class ContentBrowserViewModel
{
    /// <summary>Shows a source/dependency identity in its folder, clearing filters only when they would hide it.</summary>
    /// <param name="assetUri">The asset to reveal.</param>
    /// <returns>Whether the requested identity became the current browser selection.</returns>
    public async Task<bool> ShowAssetAsync(Uri assetUri)
    {
        ArgumentNullException.ThrowIfNull(assetUri);
        if (this.isDisposed || this.childContainer is null || this.localRouter is null || this.browserState is null)
        {
            return false;
        }

        var project = projectContextService.ActiveProject;
        var item = await this.childContainer.Resolve<IContentBrowserAssetProvider>().ResolveAsync(assetUri).ConfigureAwait(true);
        return item is not null && item.PrimaryState != AssetState.Missing && ReferenceEquals(project, projectContextService.ActiveProject)
            && !this.isDisposed && project is not null && await this.RevealAssetsAsync(project, [item]).ConfigureAwait(true);
    }

    /// <summary>Reveals available imported outputs together without resolving or rescanning each row individually.</summary>
    /// <param name="assetUris">Actual native output identities from the selected cook.</param>
    /// <returns>Whether any requested output became visible.</returns>
    public async Task<bool> ShowAssetsAsync(IReadOnlyCollection<Uri> assetUris)
    {
        ArgumentNullException.ThrowIfNull(assetUris);
        if (assetUris.Count == 0 || this.isDisposed || this.childContainer is null || this.localRouter is null || this.browserState is null
            || projectContextService.ActiveProject is not { } project)
        {
            return false;
        }

        var provider = this.childContainer.Resolve<IContentBrowserAssetProvider>();
        await provider.RefreshAsync(AssetBrowserFilter.Default).ConfigureAwait(true);
        var items = await provider.Items.FirstAsync().ToTask().ConfigureAwait(true);
        var requested = assetUris.ToHashSet();
        var available = items.Where(item => item.PrimaryState != AssetState.Missing
            && (requested.Contains(item.IdentityUri) || (item.CookedUri is { } cooked && requested.Contains(cooked)))).ToArray();
        return available.Length > 0 && !this.isDisposed && ReferenceEquals(project, projectContextService.ActiveProject)
            && await this.RevealAssetsAsync(project, available).ConfigureAwait(true);
    }

    private static bool IsCookedOnly(ContentBrowserAssetItem item)
        => item.SourcePath is null && item.DescriptorPath is null && !item.IsBuiltin;

    private static string? LibraryFolder(ProjectContext project, ContentBrowserAssetItem item)
    {
        if (item.CookedMetadata is not { } source)
        {
            return null;
        }

        var library = project.LocalFolderMounts.FirstOrDefault(mount => string.Equals(
            Path.TrimEndingDirectorySeparator(Path.GetFullPath(mount.AbsolutePath)),
            Path.TrimEndingDirectorySeparator(Path.GetFullPath(source.RootFolderPath)),
            StringComparison.OrdinalIgnoreCase));
        if (library is null)
        {
            return null;
        }

        var relative = source.DescriptorRelativePath.Replace('\\', '/');
        var slash = relative.LastIndexOf('/');
        return "/" + library.Name + (slash < 0 ? string.Empty : "/" + relative[..slash]);
    }

    private async Task<bool> RevealAssetsAsync(ProjectContext project, ContentBrowserAssetItem[] items)
    {
        var needsCooked = items.Any(item => IsCookedOnly(item) && LibraryFolder(project, item) is null);
        if (needsCooked && ProjectCookedFolders.Roots(project).Length == 0)
        {
            await this.localRouter!.NavigateAsync("/(left:project//right:" + this.currentAssetsViewPath + ")").ConfigureAwait(true);
            if (this.LeftPaneViewModel is not ProjectLayoutViewModel explorer || !ReferenceEquals(project, projectContextService.ActiveProject))
            {
                return false;
            }

            await explorer.MountKnownLocationCommand.ExecuteAsync(KnownVirtualFolderMount.Cooked).ConfigureAwait(true);
            if (projectContextService.ActiveProject is not { } mounted || mounted.ProjectId != project.ProjectId
                || !string.Equals(mounted.ProjectRoot, project.ProjectRoot, StringComparison.OrdinalIgnoreCase)
                || ProjectCookedFolders.Roots(mounted).Length == 0)
            {
                return false;
            }

            project = mounted;
        }

        var cookedFolder = ProjectCookedFolders.Roots(project).FirstOrDefault();
        var folders = items.Select(item =>
        {
            var path = AssetUriHelper.GetVirtualPath(item.IdentityUri);
            var folder = path[..path.LastIndexOf('/')];
            return IsCookedOnly(item) ? (LibraryFolder(project, item) ?? (cookedFolder + folder)) : folder;
        }).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        if (items.Any(item => !this.Query.Matches(item)))
        {
            this.Query.ClearAllCommand.Execute(parameter: null);
        }

        await this.localRouter!.NavigateAsync("/(left:project//right:" + this.currentAssetsViewPath + ")" + RouteStateMapping.BuildSelectedQuery(folders)).ConfigureAwait(true);
        if (this.browserState?.ActiveAssetLayout is not { } layout || this.isDisposed || !ReferenceEquals(project, projectContextService.ActiveProject))
        {
            return false;
        }

        layout.SelectedAsset = items[0];
        layout.RevealSelection();
        return layout.SelectedAsset is { } selected && AssetIdentityGrouping.Represents(selected, items[0].IdentityUri);
    }
}
