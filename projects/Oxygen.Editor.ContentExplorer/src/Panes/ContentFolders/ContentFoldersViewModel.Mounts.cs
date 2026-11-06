// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reactive.Concurrency;
using DroidNet.Aura.Dialogs;
using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>Separates mount presentation, persistence and restoration operations.</summary>
public partial class ContentFoldersViewModel
{
    // A new publication reaches the catalog as a burst of record changes; one check on the UI thread covers the burst.
    private void OnCatalogChanged()
    {
        if (Interlocked.Exchange(ref this.publishedFoldersCheckQueued, 1) == 0)
        {
            _ = hostingContext.DispatcherScheduler.Schedule(() =>
            {
                Volatile.Write(ref this.publishedFoldersCheckQueued, 0);
                _ = this.RefreshPublishedFoldersAsync();
            });
        }
    }

    // Folders created, deleted or renamed, in the editor or outside it, arrive from the catalog as a burst; one reload on
    // the UI thread covers the burst.
    private void OnFoldersChanged()
    {
        if (Interlocked.Exchange(ref this.folderReloadQueued, 1) == 0)
        {
            _ = hostingContext.DispatcherScheduler.Schedule(() =>
            {
                Volatile.Write(ref this.folderReloadQueued, 0);
                if (!this.selectionDisposed)
                {
                    this.LogReloadingTreeAfterFolderChanges();
                    _ = this.ReloadMountTreeAsync();
                }
            });
        }
    }

    // Reloads the tree only when the project's cooked outputs differ from the cooked folders it shows, so that
    // ordinary source changes leave the tree alone.
    private async Task RefreshPublishedFoldersAsync()
    {
        if (this.selectionDisposed)
        {
            return;
        }

        var project = session.Context;
        if (!project.OutputFolderViews.Any(static view => string.Equals(view.Folder, ProjectOutputFolders.Cooked, StringComparison.Ordinal)))
        {
            return;
        }

        try
        {
            if (await this.ShowsCurrentCookedOutputsAsync(project).ConfigureAwait(true))
            {
                return;
            }

            await this.ReloadMountTreeAsync().ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            // Project closure or a newer tree refresh supersedes this projection.
        }
        catch (Exception failure) when (failure is IOException or InvalidDataException or InvalidOperationException)
        {
            this.LogCookedProjectionFailure(failure);
        }
    }

    private async Task<bool> ShowsCurrentCookedOutputsAsync(ProjectContext project)
    {
        if (this.ShownItems.OfType<ProjectRootTreeItemAdapter>().FirstOrDefault() is not { } root)
        {
            return false;
        }

        var children = await root.Children.ConfigureAwait(true);
        if (children.OfType<VirtualFolderMountTreeItemAdapter>().FirstOrDefault(static mount => mount.CookedVirtualPaths is not null) is not { } cooked)
        {
            return false;
        }

        using var selected = await publications.AcquireReadAsync(project, CancellationToken.None).ConfigureAwait(true);
        return ReferenceEquals(project, session.Context)
            && selected.ProjectOutputPaths.Distinct(StringComparer.Ordinal).SequenceEqual(cooked.CookedVirtualPaths!, StringComparer.Ordinal);
    }

    private async Task<bool> ShowLocalMountDialogAsync(LocalFolderMountDialogViewModel model)
    {
        if (this.vmToView.Convert(model, typeof(object), parameter: null, language: System.Globalization.CultureInfo.CurrentUICulture.Name) is not UIElement view)
        {
            throw new InvalidOperationException("VmToViewConverter returned null UIElement for LocalFolderMountDialogViewModel");
        }

        if (view is DroidNet.Mvvm.IViewFor viewFor)
        {
            viewFor.ViewModel = model;
        }

        var spec = new DialogSpec("Mount Local Folder", view)
        {
            PrimaryButtonText = "Add",
            SecondaryButtonText = "Cancel",
            CloseButtonText = string.Empty,
            DefaultButton = DialogButton.Primary,
        };
        return await dialogService.ShowAsync(spec, session.Lifetime).ConfigureAwait(true) == DialogButton.Primary;
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A failed mount is reported independently so remaining persisted mounts can still be restored.")]
    private async Task RestoreMountAsync(ProjectRootTreeItemAdapter root, ProjectContext context, string name, string backingPath, bool isProjectRelative, bool expanded)
    {
        try
        {
            var location = isProjectRelative ? storage.NormalizeRelativeTo(root.ProjectRootFolder.Location, backingPath) : backingPath;
            var folder = await storage.GetFolderFromPathAsync(location, session.Lifetime).ConfigureAwait(true);
            if (this.selectionDisposed || !ReferenceEquals(context, session.Context))
            {
                return;
            }

            IReadOnlyList<string>? outputPaths = null;
            if (isProjectRelative && string.Equals(backingPath, ProjectOutputFolders.Cooked, StringComparison.Ordinal) && !this.selectionDisposed)
            {
                try
                {
                    using var selected = await publications.AcquireReadAsync(context, CancellationToken.None).ConfigureAwait(true);
                    outputPaths = [.. selected.ProjectOutputPaths.Distinct(StringComparer.Ordinal)];
                    contentExplorerState.SetOutdatedPublication(reason: null);
                }
                catch (CookPublicationFormatException outdated)
                {
                    // The assets pane offers Cook project, which upgrades it; the tree shows the cooked folder again
                    // once the cook publishes.
                    this.LogOutdatedPublication(outdated.Version);
                    contentExplorerState.SetOutdatedPublication(outdated.Message);
                    return;
                }
            }

            VirtualFolderMountTreeItemAdapter? mount = null;
            try
            {
                mount = new(this.logger, name, folder, backingPath, isProjectRelative ? VirtualFolderMountBackingPathKind.ProjectRelative : VirtualFolderMountBackingPathKind.Absolute)
                {
                    IsExpanded = expanded,
                    CookedVirtualPaths = outputPaths,
                };
                if (await root.MountVirtualFolderAsync(mount).ConfigureAwait(true))
                {
                    mount = null;
                }
            }
            finally
            {
                mount?.Dispose();
            }
        }
        catch (Exception exception)
        {
            this.LogMountRestoreFailure(exception, name);
        }
    }

    // The engine content is listed first, then each library that can be read; a library's folders are those of what it
    // holds, below its mount.
    private async Task MountLibrariesAsync(ProjectRootTreeItemAdapter root, ProjectContext project)
    {
        try
        {
            var engine = await engineContent.ReadAsync(CancellationToken.None).ConfigureAwait(true);
            _ = root.MountLibrary(new(AssetPath.Engine.Mount, engineContent.Path, isPak: true, BelowMount(AssetPath.Engine.Mount, engine.Assets.Select(static asset => asset.VirtualPath))));
        }
        catch (Exception failure) when (failure is IOException or InvalidDataException or UnauthorizedAccessException)
        {
            this.LogPakMountFailure(failure, AssetPath.Engine.Mount, engineContent.Path);
        }

        foreach (var mount in project.LibraryMounts)
        {
            var paths = mount.Library!.Assets.Select(static asset => asset.VirtualPath).Concat(mount.Library.Textures.Keys);
            _ = root.MountLibrary(new(mount.Name, mount.Location!, mount.Container == MountContainer.Pak, BelowMount(mount.Name, paths)) { IsExpanded = mount.IsExpanded });
        }
    }

    private static IEnumerable<string> BelowMount(string mount, IEnumerable<string> virtualPaths)
    {
        var prefix = "/" + mount + "/";
        return virtualPaths.Where(path => path.StartsWith(prefix, StringComparison.Ordinal)).Select(path => path[prefix.Length..]);
    }
}
