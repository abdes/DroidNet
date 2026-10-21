// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls;
using DroidNet.Controls.Menus;
using DroidNet.Storage;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>
/// New Folder, folder renames, moves, copies and deletes. A folder's path is part of every asset identity inside it,
/// so renaming or moving a populated folder is a relocation that updates every reference to its assets.
/// </summary>
public partial class ContentFoldersViewModel
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
            var target = RelocationPaths.Rename(before, name, isFolder: true);
            var request = new AssetRelocationRequest { Moves = [new(before, target)] };
            this.LogFolderRenameRequested(before, target);
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
        if (before is not null && contentExplorerState.SelectedFolders.Contains(before) && this.GetVirtualPath(folder) is { } after)
        {
            contentExplorerState.SetSelectedFolders(contentExplorerState.SelectedFolders.Select(path => path == before ? after : path));
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

        var sources = request.Items.Select(this.GetVirtualPath).OfType<AssetPath>().ToArray();
        this.LogFolderDropRequested(request.Operation, sources, parent);

        if (request.Operation == TreeDropOperation.Copy)
        {
            return (await relocation.CopyAsync(sources, parent, "Copy folders").ConfigureAwait(true)).Succeeded
                ? TreeDropResult.Committed(request.Items) : TreeDropResult.Rejected;
        }

        var moves = sources.Where(source => source.Parent != parent)
            .Select(source => new AssetRelocationMove(source, parent.Combine(source.Name))).ToArray();
        return moves.Length != 0 && await relocation.RelocateAsync(new AssetRelocationRequest { Moves = moves }, "Move folders").ConfigureAwait(true) is { Succeeded: true }
            ? TreeDropResult.Committed(request.Items)
            : TreeDropResult.Rejected;
    }

    /// <inheritdoc />
    /// <remarks>
    /// The folders go on the Content Explorer's clipboard instead of the tree's own, which copies rows only, so Paste,
    /// in the tree or the asset list, copies them as folders.
    /// </remarks>
    public override Task CopyItemsAsync(IReadOnlyList<ITreeItem> items)
    {
        this.PutOnClipboard(items, isCut: false);
        return Task.CompletedTask;
    }

    /// <inheritdoc />
    /// <remarks>The folders go on the Content Explorer's clipboard; the browser's Paste moves them.</remarks>
    public override Task CutItemsAsync(IReadOnlyList<ITreeItem> items)
    {
        this.PutOnClipboard(items, isCut: true);
        return Task.CompletedTask;
    }

    /// <summary>Pastes the Content Explorer's clipboard into the focused folder, as the asset list's Paste does.</summary>
    /// <returns>Completion after the paste, or at once when there is nothing to paste or no folder to paste into.</returns>
    public Task PasteIntoFocusedFolderAsync()
        => (this.FocusedItem?.Item ?? this.SelectedItem) is { } target && IsWritable(target) && this.GetVirtualPath(target) is { } folder
            ? this.PasteIntoAsync(folder)
            : Task.CompletedTask;

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
                Text = "Cut",
                Icon = new Microsoft.UI.Xaml.Controls.SymbolIconSource { Symbol = Microsoft.UI.Xaml.Controls.Symbol.Cut },
                Command = new RelayCommand(() => contentExplorerState.SetClipboard(folders, isCut: true)),
            });
            _ = builder.AddMenuItem(new MenuItemData
            {
                Text = "Copy",
                Icon = new Microsoft.UI.Xaml.Controls.SymbolIconSource { Symbol = Microsoft.UI.Xaml.Controls.Symbol.Copy },
                Command = new RelayCommand(() => contentExplorerState.SetClipboard(folders, isCut: false)),
            });
            if (contentExplorerState.Clipboard.Count != 0)
            {
                _ = builder.AddMenuItem(new MenuItemData
                {
                    Text = "Paste",
                    Icon = new Microsoft.UI.Xaml.Controls.SymbolIconSource { Symbol = Microsoft.UI.Xaml.Controls.Symbol.Paste },
                    Command = new AsyncRelayCommand(() => this.PasteIntoAsync(folders[0])),
                });
            }

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

    /// <summary>Creates a folder on disk under a writable folder, shows it in the tree and starts naming it in place.</summary>
    /// <param name="parentFolder">The folder to create it in.</param>
    /// <returns>A task that completes once the folder is created and its name editor opened, or the failure shown.</returns>
    [SuppressMessage("Reliability", "CA2000:Dispose objects before losing scope", Justification = "the tree owns the row it is given")]
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The UI operation boundary reports failures and leaves the tree unchanged.")]
    internal async Task CreateFolderAsync(AssetPath parentFolder)
    {
        try
        {
            var parent = await this.FindAdapterByVirtualPathAsync(parentFolder).ConfigureAwait(true) as TreeItemAdapter;
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
    private AssetPath[] GetAuthoredFolders(IEnumerable<ITreeItem> items)
        => [.. items.OfType<FolderTreeItemAdapter>().Where(IsUnderAuthoringMount).Select(this.GetVirtualPath).OfType<AssetPath>()];

    private async Task DeleteFoldersAsync(AssetPath[] folders)
    {
        if (relocation is null || folders.Length == 0)
        {
            return;
        }

        this.LogFolderDeleteRequested(folders);
        _ = await relocation.DeleteAsync(folders).ConfigureAwait(true);
    }

    private async Task PasteIntoAsync(AssetPath folder)
    {
        if (relocation is null || contentExplorerState.Clipboard.Count == 0)
        {
            return;
        }

        var isCut = contentExplorerState.IsClipboardCut;
        if (await relocation.PasteAsync(contentExplorerState.Clipboard, isCut, folder).ConfigureAwait(true) is { Succeeded: true } && isCut)
        {
            contentExplorerState.ClearClipboard();
        }
    }

    // A selection without any authored folder leaves the clipboard as it is.
    private void PutOnClipboard(IReadOnlyList<ITreeItem> items, bool isCut)
    {
        var authored = this.GetAuthoredFolders(items);
        if (authored.Length != 0)
        {
            contentExplorerState.SetClipboard(authored, isCut);
        }
    }

    // Folders that moved, were created or were deleted change the tree and the selected folders; reload both from disk.
    private void OnFilesChanged(object? sender, AssetFilesChangedEventArgs args) => _ = this.FollowChangedFilesAsync(args);

    private async Task FollowChangedFilesAsync(AssetFilesChangedEventArgs message)
    {
        // The tree shows folders only: a file move, rename or delete leaves it as it is.
        if (!message.FoldersChanged)
        {
            return;
        }

        var project = session.Context;

        var moves = message.Moves.Where(static move => move.IsDirectory)
            .Select(move => (From: RelocationPaths.ToVirtual(project, move.Source), To: RelocationPaths.ToVirtual(project, move.Target)))
            .Where(static move => move.From is not null && move.To is not null).ToArray();
        AssetPath Map(AssetPath folder)
        {
            foreach (var (from, to) in moves)
            {
                if (folder.IsSameOrInside(from!))
                {
                    return folder == from ? to! : to!.Combine(folder.GetPathBelow(from!));
                }
            }

            return folder;
        }

        this.LogReloadingTreeAfterFileChanges(moves.Length, message.DeletedFiles.Count);
        var selected = contentExplorerState.SelectedFolders.Select(Map).ToArray();
        if (!selected.SequenceEqual(contentExplorerState.SelectedFolders))
        {
            contentExplorerState.SetSelectedFolders(selected);
        }

        await this.ReloadMountTreeAsync().ConfigureAwait(true);
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
