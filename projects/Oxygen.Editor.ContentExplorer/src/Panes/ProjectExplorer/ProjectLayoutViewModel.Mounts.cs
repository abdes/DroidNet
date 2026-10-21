// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reactive.Concurrency;
using DroidNet.Aura.Dialogs;
using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Persistence.Pak;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>Separates mount presentation, persistence and restoration operations.</summary>
public partial class ProjectLayoutViewModel
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
        if (this.selectionDisposed || projectContextService.ActiveProject is not { } project
            || !project.AuthoringMounts.Any(static mount => string.Equals(mount.RelativePath.Replace('\\', '/').Trim('/'), ".cooked", StringComparison.OrdinalIgnoreCase)))
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
        return ReferenceEquals(project, projectContextService.ActiveProject)
            && selected.ProjectOutputPaths.Distinct(StringComparer.Ordinal).SequenceEqual(cooked.CookedVirtualPaths!, StringComparer.Ordinal);
    }

    private static string? GetRelativeMountPath(FileSystemPath projectRoot, FileSystemPath path)
        => !path.IsSameOrInside(projectRoot) ? null : path == projectRoot ? "." : path.GetPathBelow(projectRoot);

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
        return await dialogService.ShowAsync(spec).ConfigureAwait(true) == DialogButton.Primary;
    }

    private void AddMountToProjectInfo(ProjectInfo projectInfo, ITreeItem child)
    {
        string name;
        string path;
        string? relative;
        switch (child)
        {
            case VirtualFolderMountTreeItemAdapter mount:
                name = mount.MountPointName;
                path = mount.RootFolder.Location;
                relative = mount.BackingPathKind == VirtualFolderMountBackingPathKind.ProjectRelative ? mount.BackingPath : null;
                break;
            case AuthoringMountPointTreeItemAdapter mount:
                name = mount.MountPoint.Name;
                path = mount.RootFolder.Location;
                relative = null;
                break;
            default:
                return;
        }

        var folder = FileSystemPath.From(path);
        var wasLocal = projectContextService.ActiveProject?.LocalFolderMounts.Any(mount => mount.Root == folder) == true;
        if (!wasLocal)
        {
            relative ??= GetRelativeMountPath(FileSystemPath.From(this.projectRoot!.ProjectRootFolder.Location), folder);
        }

        if (relative is not null)
        {
            projectInfo.AuthoringMounts.Add(new ProjectMountPoint(name, relative, child.IsExpanded));
        }
        else
        {
            projectInfo.LocalFolderMounts.Add(new LocalFolderMount(name, folder, child.IsExpanded));
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A failed mount is reported independently so remaining persisted mounts can still be restored.")]
    private async Task RestoreMountAsync(ProjectRootTreeItemAdapter root, ProjectContext context, string name, string backingPath, bool isProjectRelative, bool expanded)
    {
        try
        {
            var location = isProjectRelative ? storage.NormalizeRelativeTo(root.ProjectRootFolder.Location, backingPath) : backingPath;
            var folder = await storage.GetFolderFromPathAsync(location).ConfigureAwait(true);
            if (this.selectionDisposed || !ReferenceEquals(context, projectContextService.ActiveProject))
            {
                return;
            }

            IReadOnlyList<string>? outputPaths = null;
            if (isProjectRelative && string.Equals(backingPath.Replace('\\', '/').Trim('/'), ".cooked", StringComparison.OrdinalIgnoreCase)
                && !this.selectionDisposed)
            {
                using var selected = await publications.AcquireReadAsync(context, CancellationToken.None).ConfigureAwait(true);
                outputPaths = selected.ProjectOutputPaths.Distinct(StringComparer.Ordinal).ToArray();
            }

            VirtualFolderMountTreeItemAdapter? mount = null;
            try
            {
                mount = new(this.logger, name, folder, backingPath, isProjectRelative ? VirtualFolderMountBackingPathKind.ProjectRelative : VirtualFolderMountBackingPathKind.Absolute)
                {
                    IsExpanded = expanded,
                    CookedVirtualPaths = outputPaths,
                };
                mount.PropertyChanged += this.OnMountPointPropertyChanged;
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

    // The engine content is listed first, its folders relative to /Engine; a declared pak's folders are its assets'
    // whole virtual paths, under the pak's mount name. A pak that cannot be read is reported and left out.
    private async Task MountPaksAsync(ProjectRootTreeItemAdapter root, IProjectInfo projectInfo)
    {
        try
        {
            var engine = await engineContent.ReadAsync(CancellationToken.None).ConfigureAwait(true);
            var below = AssetPath.Engine.VirtualPath + "/";
            _ = root.MountPak(new(AssetPath.Engine.Mount, engineContent.Path, engine.Assets
                .Where(asset => asset.VirtualPath.StartsWith(below, StringComparison.Ordinal))
                .Select(asset => asset.VirtualPath[below.Length..])));
        }
        catch (Exception failure) when (failure is IOException or InvalidDataException or UnauthorizedAccessException)
        {
            this.LogPakMountFailure(failure, AssetPath.Engine.Mount, engineContent.Path);
        }

        foreach (var mount in projectInfo.PakFileMounts)
        {
            try
            {
                var pak = await PakIndex.ReadFileAsync(mount.Path, CancellationToken.None).ConfigureAwait(true);
                _ = root.MountPak(new(mount.Name, mount.Path, pak.Assets.Select(static asset => asset.VirtualPath.TrimStart('/'))) { IsExpanded = mount.IsExpanded });
            }
            catch (Exception failure) when (failure is IOException or InvalidDataException or UnauthorizedAccessException)
            {
                this.LogPakMountFailure(failure, mount.Name, mount.Path);
            }
        }
    }
}
