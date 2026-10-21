// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using System.Reactive.Threading.Tasks;
using DryIoc;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentExplorer.Panes.Assets;
using Oxygen.Editor.ContentExplorer.Panes.ContentFolders;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer;

/// <summary>Handles explicit navigation to asset identities without changing cook state.</summary>
public sealed partial class ContentExplorerViewModel
{
    /// <summary>Shows a source/dependency identity in its folder, clearing filters only when they would hide it.</summary>
    /// <param name="asset">The asset to reveal.</param>
    /// <returns>Whether the requested identity became the current browser selection.</returns>
    public async Task<bool> ShowAssetAsync(AssetPath asset)
    {
        ArgumentNullException.ThrowIfNull(asset);
        if (this.isDisposed || this.scope is null || this.localRouter is null || this.browserState is null)
        {
            return false;
        }

        var project = session.Context;
        var item = await this.scope.Resolver.Resolve<IAssetFeed>().ResolveAsync(asset).ConfigureAwait(true);
        return item is not null && item.PrimaryState != AssetState.Missing && ReferenceEquals(project, session.Context)
            && !this.isDisposed && await this.RevealAssetsAsync(project, [item]).ConfigureAwait(true);
    }

    /// <summary>Reveals available imported outputs together without resolving or rescanning each row individually.</summary>
    /// <param name="assets">Actual native output identities from the selected cook.</param>
    /// <returns>Whether any requested output became visible.</returns>
    public async Task<bool> ShowAssetsAsync(IReadOnlyCollection<AssetPath> assets)
    {
        ArgumentNullException.ThrowIfNull(assets);
        if (assets.Count == 0 || this.isDisposed || this.scope is null || this.localRouter is null || this.browserState is null)
        {
            return false;
        }

        var project = session.Context;

        var provider = this.scope.Resolver.Resolve<IAssetFeed>();
        await provider.RefreshAsync().ConfigureAwait(true);
        var items = await provider.Items.FirstAsync().ToTask().ConfigureAwait(true);
        var requested = assets.ToHashSet();
        var available = items.Where(item => item.PrimaryState != AssetState.Missing && requested.Contains(item.Identity)).ToArray();
        return available.Length > 0 && !this.isDisposed && ReferenceEquals(project, session.Context)
            && await this.RevealAssetsAsync(project, available).ConfigureAwait(true);
    }

    private static bool IsCookedOnly(AssetEntry item)
        => item.SourcePath is null && item.DescriptorPath is null && !item.IsPackaged;

    private static AssetPath? LibraryFolder(ProjectContext project, AssetEntry item)
        => item.CookedMetadata is { } source
            && project.LocalFolderMounts.FirstOrDefault(mount => mount.Root == source.ContainerPath) is { } library
            && AssetPath.TryCreate(library.Name, source.DescriptorRelativePath, out var location)
                ? location.Parent
                : null;

    private async Task<bool> RevealAssetsAsync(ProjectContext project, AssetEntry[] items)
    {
        var needsCooked = items.Any(item => IsCookedOnly(item) && LibraryFolder(project, item) is null);
        if (needsCooked && ProjectCookedFolders.Roots(project).Length == 0)
        {
            await this.localRouter!.NavigateAsync("/(left:folders//right:" + this.currentAssetsViewPath + ")").ConfigureAwait(true);
            if (this.LeftPaneViewModel is not ContentFoldersViewModel explorer || !ReferenceEquals(project, session.Context))
            {
                return false;
            }

            await explorer.MountKnownLocationCommand.ExecuteAsync(KnownVirtualFolderMount.Cooked).ConfigureAwait(true);
            var mounted = session.Context;
            if (mounted.ProjectId != project.ProjectId
                || mounted.ProjectRoot != project.ProjectRoot
                || ProjectCookedFolders.Roots(mounted).Length == 0)
            {
                return false;
            }

            project = mounted;
        }

        var cookedFolder = ProjectCookedFolders.Roots(project).FirstOrDefault();
        // Project output shows under the mount of its cooked folder, at its native path.
        var folders = items.Select(item =>
        {
            var folder = item.Identity.Parent!;
            return IsCookedOnly(item) ? LibraryFolder(project, item) ?? cookedFolder?.Combine(folder.VirtualPath.TrimStart('/')) ?? folder : folder;
        }).Distinct().ToArray();
        if (items.Any(item => !this.Query.Matches(item)))
        {
            this.Query.ClearAllCommand.Execute(parameter: null);
        }

        await this.localRouter!.NavigateAsync("/(left:folders//right:" + this.currentAssetsViewPath + ")" + RouteStateMapping.BuildSelectedQuery(folders)).ConfigureAwait(true);
        if (this.browserState?.ActiveAssetLayout is not { } layout || this.isDisposed || !ReferenceEquals(project, session.Context))
        {
            return false;
        }

        layout.SelectedAsset = items[0];
        layout.RevealSelection();
        return layout.SelectedAsset is { } selected && selected.Identity == items[0].Identity;
    }
}
