// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls;
using DroidNet.Controls.Menus;
using DroidNet.Storage;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>
/// New Folder, folder renames, moves, copies and deletes. A folder's path is part of every asset identity inside it,
/// so renaming or moving a populated folder is a relocation that updates every reference to its assets.
/// </summary>
public partial class ProjectLayoutViewModel
{
    /// <summary>The name a new folder starts with, before the user names it in place.</summary>
    internal const string NewFolderName = "New folder";

    /// <summary>Why a folder that holds files cannot be renamed without the relocation workflow.</summary>
    internal const string FolderRenameUnavailableReason
        = "Renaming a folder that holds assets needs the project's relocation service, which is not available here.";

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

        var name = VirtualPath.NormalizeTypedName(newName);
        if (string.Equals(name, folder.Label, StringComparison.Ordinal))
        {
            return TreeItemRenameResult.Success;
        }

        if (VirtualPath.ValidateSegment(name, VirtualPathRuleSet.Authored) is { } invalid)
        {
            return TreeItemRenameResult.Rejected(invalid.ToMessage());
        }

        if (GetFolderRenameBlocker(folder, relocation is not null) is { } reason)
        {
            return TreeItemRenameResult.Rejected(reason);
        }

        var before = this.GetVirtualPath(folder);
        if (relocation is not null && before is not null)
        {
            var request = new AssetRelocationRequest { Moves = [new(before, RelocationPaths.Rename(before, name, isFolder: true))] };
            this.LogFolderRenameRequested(before, request.Moves[0].TargetPath);
            var outcome = await relocation.RelocateAsync(request, "Rename folder").ConfigureAwait(true);
            return outcome switch
            {
                null => Renamed(folder, name),
                { Succeeded: false } => TreeItemRenameResult.Rejected(outcome.Message),
                _ => Renamed(folder, name),
            };
        }

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

    /// <inheritdoc />
    /// <remarks>
    /// Dragging folders onto an authoring folder moves them, or copies them with Ctrl, through the relocation
    /// workflow; the tree reloads from disk afterwards, so rows are not moved here.
    /// </remarks>
    public override async Task<TreeDropResult> CommitDropAsync(TreeDropRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        if (relocation is null || request.Items.Count == 0 || this.GetVirtualPath(request.Parent) is not { } parent
            || request.Parent is not (FolderTreeItemAdapter or AuthoringMountPointTreeItemAdapter) || !IsWritable(request.Parent)
            || request.Items.Any(static item => item is not FolderTreeItemAdapter folder || !IsUnderAuthoringMount(folder)))
        {
            return TreeDropResult.Rejected;
        }

        var sources = request.Items.Select(this.GetVirtualPath).OfType<string>().ToArray();
        this.LogFolderDropRequested(request.Operation, sources, parent);

        if (request.Operation == TreeDropOperation.Copy)
        {
            return (await relocation.CopyAsync(sources, parent, "Copy folders").ConfigureAwait(true)).Succeeded
                ? TreeDropResult.Committed(request.Items) : TreeDropResult.Rejected;
        }

        var moves = sources.Where(source => !string.Equals(RelocationPaths.GetParent(source), parent, StringComparison.Ordinal))
            .Select(source => new AssetRelocationMove(source, RelocationPaths.Combine(parent, RelocationPaths.GetName(source)))).ToArray();
        return moves.Length != 0 && await relocation.RelocateAsync(new AssetRelocationRequest { Moves = moves }, "Move folders").ConfigureAwait(true) is { Succeeded: true }
            ? TreeDropResult.Committed(request.Items)
            : TreeDropResult.Rejected;
    }

    /// <inheritdoc />
    /// <remarks>
    /// The folders go on the Content Browser's clipboard instead of the tree's own, which copies rows only, so Paste,
    /// in the tree or the asset list, copies them as folders.
    /// </remarks>
    public override Task CopyItemsAsync(IReadOnlyList<ITreeItem> items)
    {
        this.PutOnClipboard(items, isCut: false);
        return Task.CompletedTask;
    }

    /// <inheritdoc />
    /// <remarks>The folders go on the Content Browser's clipboard; the browser's Paste moves them.</remarks>
    public override Task CutItemsAsync(IReadOnlyList<ITreeItem> items)
    {
        this.PutOnClipboard(items, isCut: true);
        return Task.CompletedTask;
    }

    /// <summary>Pastes the Content Browser's clipboard into the focused folder, as the asset list's Paste does.</summary>
    /// <returns>Completion after the paste, or at once when there is nothing to paste or no folder to paste into.</returns>
    public async Task PasteIntoFocusedFolderAsync()
    {
        if (relocation is null || contentBrowserState.Clipboard.Count == 0
            || (this.FocusedItem?.Item ?? this.SelectedItem) is not { } target || !IsWritable(target) || this.GetVirtualPath(target) is not { } folder)
        {
            return;
        }

        var isCut = contentBrowserState.IsClipboardCut;
        if (await relocation.PasteAsync(contentBrowserState.Clipboard, isCut, folder).ConfigureAwait(true) is { Succeeded: true } && isCut)
        {
            contentBrowserState.ClearClipboard();
        }
    }

    /// <summary>Deletes the selected folders, through the same confirmation and Recycle Bin as the asset list.</summary>
    /// <returns>Completion after the delete, or at once when no selected folder can be deleted.</returns>
    public Task DeleteSelectedFoldersAsync() => this.DeleteFoldersAsync(this.GetAuthoredFolders(this.ShownItems.Where(static item => item.IsSelected)));

    /// <summary>Builds the context menu of a folder row.</summary>
    /// <param name="item">The row the menu was requested on.</param>
    /// <returns>The menu; it has no items for rows that are not folders under an authoring mount.</returns>
    public IMenuSource BuildFolderMenu(ITreeItem item)
    {
        var builder = new MenuBuilder();
        if (relocation is not null && this.GetAuthoredFolders([item]) is { Length: > 0 } folders)
        {
            _ = builder.AddMenuItem(new MenuItemData
            {
                Text = "Delete",
                Icon = new Microsoft.UI.Xaml.Controls.SymbolIconSource { Symbol = Microsoft.UI.Xaml.Controls.Symbol.Delete },
                Command = new AsyncRelayCommand(() => this.DeleteFoldersAsync(folders)),
            });
        }

        return builder.Build();
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
    /// <param name="canRelocate">Whether populated folders can be relocated with their references.</param>
    /// <returns>The reason, or null.</returns>
    internal static string? GetFolderRenameBlocker(FolderTreeItemAdapter folder, bool canRelocate = false)
    {
        ArgumentNullException.ThrowIfNull(folder);
        if (!IsUnderAuthoringMount(folder))
        {
            return "Mounted and derived content is read-only.";
        }

        if (canRelocate)
        {
            return null;
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

    // The relocation moved the folder on disk, or nothing had to change; a reload that follows replaces this row.
    private static TreeItemRenameResult Renamed(FolderTreeItemAdapter folder, string name)
    {
        folder.Label = name;
        return TreeItemRenameResult.Success;
    }

    private static bool IsWritable(ITreeItem item)
        => item switch
        {
            AuthoringMountPointTreeItemAdapter mount => !IsPersistedProjectRelativeVirtualMount(mount.MountPoint),
            FolderTreeItemAdapter folder => IsUnderAuthoringMount(folder),
            _ => false,
        };

    private static bool IsUnderAuthoringMount(FolderTreeItemAdapter item)
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

    // The folders under an authoring mount: they can be copied, moved and deleted. Mount roots are changed in Mounts,
    // and the importer's fixed folders are rejected by the delete itself.
    private string[] GetAuthoredFolders(IEnumerable<ITreeItem> items)
        => [.. items.OfType<FolderTreeItemAdapter>().Where(IsUnderAuthoringMount).Select(this.GetVirtualPath).OfType<string>()];

    private async Task DeleteFoldersAsync(string[] folders)
    {
        if (relocation is null || folders.Length == 0)
        {
            return;
        }

        this.LogFolderDeleteRequested(folders);
        _ = await relocation.DeleteAsync(folders).ConfigureAwait(true);
    }

    // A selection without any authored folder leaves the clipboard as it is.
    private void PutOnClipboard(IReadOnlyList<ITreeItem> items, bool isCut)
    {
        var folders = this.GetAuthoredFolders(items);
        if (folders.Length != 0)
        {
            contentBrowserState.SetClipboard(folders, isCut);
        }
    }

    // Folders that moved, were created or were deleted change the tree and the selected folders; reload both from disk.
    private void OnFilesChanged(object? sender, AssetFilesChangedEventArgs args) => _ = this.FollowChangedFilesAsync(args);

    private async Task FollowChangedFilesAsync(AssetFilesChangedEventArgs message)
    {
        // The tree shows folders only: a file move, rename or delete leaves it as it is.
        if (projectContextService.ActiveProject is not { } project || !message.FoldersChanged)
        {
            return;
        }

        var moves = message.Moves.Where(static move => move.IsDirectory)
            .Select(move => (From: RelocationPaths.ToVirtual(project, move.Source), To: RelocationPaths.ToVirtual(project, move.Target)))
            .Where(static move => move.From is not null && move.To is not null).ToArray();
        string Map(string folder)
        {
            foreach (var (from, to) in moves)
            {
                if (RelocationPaths.IsSameOrInside(folder, from!))
                {
                    return to + folder[from!.Length..];
                }
            }

            return folder;
        }

        this.LogReloadingTreeAfterFileChanges(moves.Length, message.DeletedFiles.Count);
        var selected = contentBrowserState.SelectedFolders.Select(Map).ToArray();
        if (!selected.SequenceEqual(contentBrowserState.SelectedFolders, StringComparer.Ordinal))
        {
            contentBrowserState.SetSelectedFolders(selected);
        }

        await this.ReloadMountTreeAsync().ConfigureAwait(true);
    }

    // Creates the folder on disk under a writable folder, shows it in the tree and starts naming it in place.
    [SuppressMessage("Reliability", "CA2000:Dispose objects before losing scope", Justification = "the tree owns the row it is given")]
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The UI operation boundary reports failures and leaves the tree unchanged.")]
    private async Task CreateFolderAsync(CreateFolderRequestMessage request)
    {
        try
        {
            var parent = await this.FindAdapterByVirtualPathAsync(request.ParentFolder.TrimEnd('/')).ConfigureAwait(true) as TreeItemAdapter;
            var location = parent is null ? null : parent switch
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
