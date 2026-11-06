// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;
using DroidNet.Aura.Dialogs;
using DroidNet.Controls;
using DroidNet.Controls.Menus;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>
/// Binds a mount on this machine from the tree: a mount's menu binds a placeholder, overrides a mount's folder, or
/// removes the binding of the project's own <c>.mounts.json</c>, and opens the Content mounts dialog.
/// </summary>
public partial class ContentFoldersViewModel
{
    // A mount row's actions follow the row's folder actions; any other row has none.
    private void AddMountItems(MenuBuilder builder, ITreeItem item, bool afterOthers)
    {
        var name = item switch
        {
            AuthoringMountPointTreeItemAdapter source => source.MountPoint.Name,
            UnavailableMountTreeItemAdapter unavailable => unavailable.Mount.Name,
            VirtualFolderMountTreeItemAdapter library => library.MountPointName,
            PakMountTreeItemAdapter pak => pak.MountPointName,
            _ => null,
        };
        var project = session.Context;
        if (name is null || project.Mounts.FirstOrDefault(mount => string.Equals(mount.Name, name, StringComparison.Ordinal)) is not { } mount)
        {
            return;
        }

        if (afterOthers)
        {
            _ = builder.AddSeparator();
        }

        _ = builder.AddMenuItem(new MenuItemData
        {
            Text = mount.State == MountState.Unbound ? "Bind to a folder…" : "Override folder…",
            Icon = new FontIconSource { Glyph = "" },
            IsEnabled = this.CanChangeMounts,
            Command = new AsyncRelayCommand(() => this.BindMountAsync(mount.Name)),
        });
        if (mount.OriginFile == project.ProjectRoot.Combine(ProjectMounts.FileName) && mount.Scope == MountScope.Declared)
        {
            _ = builder.AddMenuItem(new MenuItemData
            {
                Text = "Remove binding",
                Icon = new FontIconSource { Glyph = "" },
                IsEnabled = this.CanChangeMounts,
                Command = new AsyncRelayCommand(() => this.ApplyMountCandidateAsync(session.Context, this.GetActiveProjectInfo(), MountBindings.None.Unbind(mount.Name))),
            });
        }

        _ = builder.AddSeparator();
        _ = builder.AddMenuItem(new MenuItemData
        {
            Text = "Open Mounts…",
            Icon = new FontIconSource { Glyph = "" },
            Command = this.ManageMountsCommand,
        });
    }

    // Binds a mount to the folder or pak file the user picks, in the project's own .mounts.json.
    private async Task BindMountAsync(string name)
    {
        var expected = session.Context;
        if (expected.Mounts.FirstOrDefault(mount => string.Equals(mount.Name, name, StringComparison.Ordinal)) is not { } mount)
        {
            return;
        }

        string? location;
        try
        {
            location = await MountLocationPicker.PickAsync(dialogService, mount.IsPakFile, mount.Location?.FullPath, session.Lifetime).ConfigureAwait(true);
        }
        catch (IOException failure)
        {
            this.LogMountChangeFailed(failure);
            await this.ShowMountUpdateFailureAsync(failure.Message).ConfigureAwait(true);
            return;
        }

        if (location is not null)
        {
            await this.ApplyMountCandidateAsync(expected, this.GetActiveProjectInfo(), MountBindings.None.Bind(name, FileSystemPath.From(location))).ConfigureAwait(true);
        }
    }

    private Task<DialogButton> ShowMountUpdateFailureAsync(string message)
        => dialogService.ShowAsync(
            new DialogSpec("Content mount update", new TextBlock { Text = message, TextWrapping = TextWrapping.Wrap, MaxWidth = 520, IsTextSelectionEnabled = true })
            {
                PrimaryButtonText = "Close",
                SecondaryButtonText = string.Empty,
                CloseButtonText = string.Empty,
                DefaultButton = DialogButton.Primary,
            },
            session.Lifetime);
}
