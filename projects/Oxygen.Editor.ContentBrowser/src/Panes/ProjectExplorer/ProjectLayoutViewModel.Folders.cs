// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using DroidNet.Controls;
using DroidNet.Storage;
using Oxygen.Editor.ContentBrowser.Messages;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>
/// New Folder and folder renames. A folder's path is part of every asset identity inside it, so only a folder
/// with no files may be renamed until relocation updates references.
/// </summary>
public partial class ProjectLayoutViewModel
{
    /// <summary>The name a new folder starts with, before the user names it in place.</summary>
    internal const string NewFolderName = "New folder";

    /// <summary>Why a folder that holds files cannot be renamed yet.</summary>
    internal const string FolderRenameUnavailableReason
        = "Not available yet: renaming a folder that holds assets must also update the scenes and materials that use them.";

    /// <inheritdoc />
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The rename boundary reports any storage failure in place and keeps the old name.")]
    public override async Task<TreeItemRenameResult> CommitRenameAsync(ITreeItem item, string newName)
    {
        ArgumentNullException.ThrowIfNull(item);
        ArgumentNullException.ThrowIfNull(newName);
        if (item is not FolderTreeItemAdapter folder)
        {
            return await base.CommitRenameAsync(item, newName).ConfigureAwait(true);
        }

        var name = newName.Trim();
        if (string.Equals(name, folder.Label, StringComparison.Ordinal))
        {
            return TreeItemRenameResult.Success;
        }

        if (!folder.ValidateItemName(name))
        {
            return TreeItemRenameResult.Rejected("Use a folder name without path separators or reserved characters.");
        }

        if (GetFolderRenameBlocker(folder) is { } reason)
        {
            return TreeItemRenameResult.Rejected(reason);
        }

        var before = this.GetVirtualPath(folder);
        try
        {
            await folder.Folder.RenameAsync(name).ConfigureAwait(true);
        }
        catch (Exception error) when (error is not OperationCanceledException)
        {
            return TreeItemRenameResult.Rejected(error.Message);
        }

        folder.Label = name;
        if (before is not null && contentBrowserState.SelectedFolders.Contains(before) && this.GetVirtualPath(folder) is { } after)
        {
            contentBrowserState.SetSelectedFolders(contentBrowserState.SelectedFolders.Select(path => string.Equals(path, before, StringComparison.Ordinal) ? after : path));
        }

        return TreeItemRenameResult.Success;
    }

    /// <summary>Returns a folder name that does not exist yet: "New folder", then "New folder (2)" and so on.</summary>
    /// <param name="exists">Tells whether a name is already taken.</param>
    /// <returns>The first free name.</returns>
    internal static string GetUniqueFolderName(Func<string, bool> exists)
    {
        ArgumentNullException.ThrowIfNull(exists);
        var name = NewFolderName;
        for (var index = 2; exists(name); index++)
        {
            name = string.Create(CultureInfo.InvariantCulture, $"{NewFolderName} ({index})");
        }

        return name;
    }

    /// <summary>Explains why a folder cannot be renamed, or returns null when it can.</summary>
    /// <param name="folder">The folder row.</param>
    /// <returns>The reason, or null.</returns>
    internal static string? GetFolderRenameBlocker(FolderTreeItemAdapter folder)
    {
        ArgumentNullException.ThrowIfNull(folder);
        if (!IsUnderAuthoringMount(folder))
        {
            return "Mounted and derived content is read-only.";
        }

        try
        {
            return Directory.EnumerateFiles(folder.Folder.Location, "*", SearchOption.AllDirectories).Any() ? FolderRenameUnavailableReason : null;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return error.Message;
        }
    }

    private static bool IsUnderAuthoringMount(ITreeItem item)
    {
        for (var current = item.Parent; current is not null; current = current.Parent)
        {
            switch (current)
            {
                case AuthoringMountPointTreeItemAdapter mount:
                    return !IsPersistedProjectRelativeVirtualMount(mount.MountPoint);
                case VirtualFolderMountTreeItemAdapter:
                    return false;
            }
        }

        return false;
    }

    // Creates the folder on disk under a writable folder, shows it in the tree and starts naming it in place.
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The UI operation boundary reports failures and leaves the tree unchanged.")]
    private async Task CreateFolderAsync(CreateFolderRequestMessage request)
    {
        try
        {
            var parent = await this.FindAdapterByVirtualPathAsync(request.ParentFolder.TrimEnd('/')).ConfigureAwait(true) as TreeItemAdapter;
            var location = parent switch
            {
                AuthoringMountPointTreeItemAdapter mount when !IsPersistedProjectRelativeVirtualMount(mount.MountPoint) => mount.RootFolder,
                FolderTreeItemAdapter folder when IsUnderAuthoringMount(folder) => folder.Folder,
                _ => null,
            };
            if (parent is null || location is null)
            {
                await dialogService.ShowMessageAsync("New folder", "Choose a folder under Content or another authoring folder.").ConfigureAwait(true);
                return;
            }

            var name = GetUniqueFolderName(candidate => Directory.Exists(Path.Combine(location.Location, candidate)) || File.Exists(Path.Combine(location.Location, candidate)));
            var created = await location.GetFolderAsync(name).ConfigureAwait(true);
            await created.CreateAsync().ConfigureAwait(true);

            await this.RevealAndExpandAsync(parent).ConfigureAwait(true);
            var children = await parent.Children.ConfigureAwait(true);
            var row = children.OfType<FolderTreeItemAdapter>().FirstOrDefault(child => string.Equals(child.Folder.Location, created.Location, StringComparison.OrdinalIgnoreCase));
            if (row is null)
            {
                row = new FolderTreeItemAdapter(this.logger, created, name);
                await this.InsertItemAsync(row, parent, children.Count).ConfigureAwait(true);
            }

            this.RenameRequested?.Invoke(this, new(row));
        }
        catch (Exception error)
        {
            await dialogService.ShowMessageAsync("New folder", error.Message).ConfigureAwait(true);
        }
    }

    private async Task RevealAndExpandAsync(ITreeItem item)
    {
        var path = new Stack<ITreeItem>();
        for (var current = item; current is not null; current = current.Parent)
        {
            path.Push(current);
        }

        while (path.TryPop(out var current))
        {
            await this.ExpandItemAsync(current).ConfigureAwait(true);
        }
    }
}
