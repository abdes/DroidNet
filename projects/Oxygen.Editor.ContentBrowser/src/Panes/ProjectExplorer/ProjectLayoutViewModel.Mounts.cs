// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Aura.Dialogs;
using DroidNet.Controls;
using DroidNet.Storage;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>Separates mount presentation, persistence and restoration operations.</summary>
public partial class ProjectLayoutViewModel
{
    private static string? GetRelativeMountPath(string projectRoot, string path)
    {
        try
        {
            var relative = Path.GetRelativePath(projectRoot, path);
            return relative.StartsWith("..", StringComparison.Ordinal) || Path.IsPathRooted(relative) ? null : relative.Replace('\\', '/');
        }
        catch (Exception exception) when (exception is ArgumentException or NotSupportedException or PathTooLongException)
        {
            return null;
        }
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
            PrimaryButtonText = "OK", SecondaryButtonText = "Cancel", CloseButtonText = string.Empty, DefaultButton = DialogButton.Primary,
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

        relative ??= GetRelativeMountPath(this.projectRoot!.ProjectRootFolder.Location, path);
        if (relative is not null)
        {
            projectInfo.AuthoringMounts.Add(new ProjectMountPoint(name, relative, child.IsExpanded));
        }
        else
        {
            projectInfo.LocalFolderMounts.Add(new LocalFolderMount(name, path, child.IsExpanded));
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A failed mount is reported independently so remaining persisted mounts can still be restored.")]
    private async Task RestoreMountAsync(IStorageProvider provider, string name, string backingPath, bool isProjectRelative, bool expanded)
    {
        try
        {
            var location = isProjectRelative ? provider.NormalizeRelativeTo(this.projectRoot!.ProjectRootFolder.Location, backingPath) : backingPath;
            var folder = await provider.GetFolderFromPathAsync(location).ConfigureAwait(true);
            VirtualFolderMountTreeItemAdapter? mount = null;
            try
            {
                mount = new(this.logger, name, folder, backingPath, isProjectRelative ? VirtualFolderMountBackingPathKind.ProjectRelative : VirtualFolderMountBackingPathKind.Absolute)
                {
                    IsExpanded = expanded,
                };
                mount.PropertyChanged += this.OnMountPointPropertyChanged;
                if (await this.projectRoot!.MountVirtualFolderAsync(mount).ConfigureAwait(true))
                {
                    var mounted = mount;
                    mount = null;
                    if (!isProjectRelative)
                    {
                        _ = this.projectAssetCatalog.AddFolderAsync(folder, mounted.VirtualRootPath.TrimStart('/'));
                    }

                    if (this.projectRoot.AreChildrenLoaded)
                    {
                        await this.InsertItemAsync(mounted, this.projectRoot, this.projectRoot.ChildrenCount).ConfigureAwait(true);
                    }
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

    [LoggerMessage(Level = LogLevel.Error, Message = "Failed to restore folder mount {Name}.")]
    private partial void LogMountRestoreFailure(Exception exception, string name);

    [LoggerMessage(Level = LogLevel.Error, Message = "Failed to navigate to folder.")]
    private partial void LogFolderNavigationFailure(Exception exception);
}
