// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentBrowser.Relocation;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentBrowser;

/// <summary>
/// Commands that act on the selected assets: open, rename, move, copy, delete, references, paths, File Explorer
/// and the cooking jobs. Renames and moves update every authored reference in one transaction.
/// </summary>
public partial class AssetsViewModel
{
    private const string ReadOnlyReason = "Cooked, built-in and mounted content is read-only.";
    private const string UnavailableReason = "Not available without an open project workspace.";

    private IReadOnlyList<string> clipboard = [];
    private bool clipboardIsCut;

    // What the selected rows are on disk, read once per selection or command-state change instead of per query.
    private IReadOnlyList<AssetTarget>? selectionTargets;

    /// <summary>Gets the selected assets of the active layout; the last is the active asset.</summary>
    public IReadOnlyList<ContentBrowserAssetItem> SelectedAssets => contentBrowserState.SelectedAssets;

    /// <summary>Gets the shared result order, view and tile size.</summary>
    public AssetBrowserPresentation Presentation => contentBrowserState.Presentation;

    /// <summary>Gets a value indicating whether the result bar offers Undo for the last rename or move.</summary>
    [ObservableProperty]
    public partial bool IsUndoAvailable { get; private set; }

    /// <summary>Gets what Rename does, or why it is unavailable.</summary>
    public string RenameToolTip => Explain("Rename (F2)", this.GetRenameBlocker(), "Content that uses it is updated.");

    /// <summary>Gets what Rename output group does, or why it is unavailable.</summary>
    public string RenameOutputGroupToolTip => Explain(
        "Rename output group",
        this.CanRenameOutputGroup() ? null : "Select one imported model, or an asset imported from one.",
        "Moves the model's Materials, Geometry and Scenes folders together.");

    /// <summary>Gets what Cut does, or why it is unavailable.</summary>
    public string CutToolTip => Explain("Cut (Ctrl+X)", this.GetTransferBlocker(), "Paste moves the items and updates the content that uses them.");

    /// <summary>Gets what Copy does, or why it is unavailable.</summary>
    public string CopyToolTip => Explain("Copy (Ctrl+C)", this.GetTransferBlocker(), "Paste creates copies with their own names.");

    /// <summary>Gets what Paste does, or why it is unavailable.</summary>
    public string PasteToolTip => Explain(
        "Paste (Ctrl+V)",
        this.clipboard.Count == 0 ? "Cut or copy items first." : this.IsSelectedFolderWritable() ? null : "Choose a folder under Content or another authoring folder.",
        this.clipboardIsCut ? "Moves the cut items here." : "Copies the items here.");

    /// <summary>Gets what Duplicate does, or why it is unavailable.</summary>
    public string DuplicateToolTip => Explain("Duplicate (Ctrl+D)", this.GetDuplicateBlocker(), "Creates a copy next to each item.");

    /// <summary>Gets what Delete does, or why it is unavailable.</summary>
    public string DeleteToolTip => Explain("Delete (Del)", this.GetDeleteBlocker(), "Moves the items to the Recycle Bin.");

    /// <summary>Gets what Find references does, or why it is unavailable.</summary>
    public string FindReferencesToolTip => Explain(
        "Find references",
        this.CanFindReferences() ? null : "Select one authored asset or folder.",
        "Shows the content that uses it in the details pane.");

    /// <summary>Gets what New Folder does, or why it is unavailable here.</summary>
    public string CreateFolderToolTip => this.CanCreateFolder()
        ? "Create a folder here and name it."
        : "Choose a folder under Content or another authoring folder. Cooked and mounted sources are read-only.";

    /// <summary>Gets what Cook Selected does, or why it is unavailable.</summary>
    public string CookSelectedToolTip => this.CanCookSelectedAsset()
        ? "Cook the selected assets."
        : "Select assets that have saved authored sources. Built-in and cooked-only assets cannot be cooked.";

    /// <summary>Gets what Cook Current Folder does, or why it is unavailable.</summary>
    public string CookFolderToolTip => this.CanCookSelectedFolder()
        ? "Cook every authored asset in this folder and its subfolders, whatever the filters show."
        : "Choose a folder under Content or another authoring folder.";

    /// <summary>Gets what Reimport does, or why it is unavailable.</summary>
    public string ReimportToolTip => this.CanReimportSelectedSource()
        ? "Import the selected source model again with its recorded settings."
        : "Select one source model, or an asset imported from one.";

    /// <summary>Gets what Show Import Source does, or why it is unavailable.</summary>
    public string ShowImportSourceToolTip => this.CanShowImportSource()
        ? "Show the source model this asset was imported from."
        : "Select one asset that was imported from a source model.";

    /// <summary>Gets what Open does, or why it is unavailable.</summary>
    public string OpenToolTip => this.CanOpenSelected() ? "Open the selected asset (Enter)." : "Select one asset to open.";

    /// <summary>Gets what Copy Path does, or why it is unavailable.</summary>
    public string CopyPathToolTip => this.CanCopyPath()
        ? "Copy the logical path of each selected asset (Ctrl+Shift+C)."
        : "Select assets to copy their paths.";

    /// <summary>Gets what Show in File Explorer does, or why it is unavailable.</summary>
    public string ShowInFileExplorerToolTip => this.CanShowInFileExplorer()
        ? "Show the selected asset's file in File Explorer."
        : "Select one asset that has a file on disk.";

    /// <summary>Returns the text Copy path places on the clipboard: one logical asset path per line.</summary>
    /// <param name="assets">The selected assets.</param>
    /// <returns>The paths, or an empty string when nothing is selected.</returns>
    public static string GetCopyPathText(IEnumerable<ContentBrowserAssetItem> assets)
    {
        ArgumentNullException.ThrowIfNull(assets);
        return string.Join(Environment.NewLine, assets.Select(static asset => AssetUriHelper.GetVirtualPath(asset.IdentityUri)));
    }

    private static string Explain(string command, string? blocker, string description)
        => command + Environment.NewLine + (blocker ?? description);

    private void NotifyAssetActions()
    {
        this.selectionTargets = null;
        this.OnPropertyChanged(nameof(this.SelectedAssets));
        this.OpenSelectedCommand.NotifyCanExecuteChanged();
        this.CopyPathCommand.NotifyCanExecuteChanged();
        this.ShowInFileExplorerCommand.NotifyCanExecuteChanged();
        this.RenameSelectedCommand.NotifyCanExecuteChanged();
        this.RenameOutputGroupCommand.NotifyCanExecuteChanged();
        this.CutSelectedCommand.NotifyCanExecuteChanged();
        this.CopySelectedCommand.NotifyCanExecuteChanged();
        this.PasteCommand.NotifyCanExecuteChanged();
        this.DuplicateSelectedCommand.NotifyCanExecuteChanged();
        this.DeleteSelectedCommand.NotifyCanExecuteChanged();
        this.FindReferencesCommand.NotifyCanExecuteChanged();
        this.UndoRelocationCommand.NotifyCanExecuteChanged();
        foreach (var name in (string[])[
            nameof(this.CreateFolderToolTip),
            nameof(this.CookSelectedToolTip),
            nameof(this.CookFolderToolTip),
            nameof(this.ReimportToolTip),
            nameof(this.ShowImportSourceToolTip),
            nameof(this.OpenToolTip),
            nameof(this.CopyPathToolTip),
            nameof(this.ShowInFileExplorerToolTip),
            nameof(this.RenameToolTip),
            nameof(this.RenameOutputGroupToolTip),
            nameof(this.CutToolTip),
            nameof(this.CopyToolTip),
            nameof(this.PasteToolTip),
            nameof(this.DuplicateToolTip),
            nameof(this.DeleteToolTip),
            nameof(this.FindReferencesToolTip),
        ])
        {
            this.OnPropertyChanged(name);
        }
    }

    private AssetTarget Describe(ContentBrowserAssetItem asset)
    {
        if (projectContextService.ActiveProject is not { } project)
        {
            return default;
        }

        var path = asset.Kind == AssetKind.Folder
            ? RelocationPaths.ToPhysical(project, Uri.UnescapeDataString(asset.IdentityUri.AbsolutePath))
            : asset.DescriptorPath ?? asset.SourcePath;
        if (path is null || !(File.Exists(path) || Directory.Exists(path)))
        {
            return new(VirtualPath: null, PhysicalPath: null, IsFolder: false, IsEditorScene: false, IsImportedOutput: asset.ImportSourceUri is not null, ModelSourcePath: asset.ImportSourcePath);
        }

        var isModel = Path.GetExtension(path).ToUpperInvariant() is ".GLTF" or ".GLB" or ".FBX" && File.Exists(path + NativeSceneImportSettings.SidecarSuffix);
        return new(
            RelocationPaths.ToVirtual(project, path),
            path,
            Directory.Exists(path),
            RelocationPaths.IsEditorScene(project, path),
            IsImportedOutput: false,
            isModel ? path : asset.ImportSourcePath);
    }

    private IReadOnlyList<AssetTarget> DescribeSelection() => this.selectionTargets ??= [.. this.SelectedAssets.Select(this.Describe)];

    private string? GetRenameBlocker()
    {
        if (relocation is null)
        {
            return UnavailableReason;
        }

        if (this.SelectedAssets.Count != 1)
        {
            return "Select one item to rename.";
        }

        var target = this.DescribeSelection()[0];
        return target.IsImportedOutput ? "This asset belongs to its imported model. Use Rename output group."
            : target.VirtualPath is null ? ReadOnlyReason : null;
    }

    private string? GetTransferBlocker()
    {
        var targets = this.DescribeSelection();
        return relocation is null ? UnavailableReason : targets.Count == 0 ? "Select items to move or copy."
            : targets.Any(static target => target.IsImportedOutput) ? "Imported assets belong to their model. Use Rename output group."
            : targets.Any(static target => target.VirtualPath is null) ? ReadOnlyReason
            : targets.Any(static target => target.IsEditorScene) ? "Scenes stay in Content/Scenes; use Rename or Duplicate."
            : null;
    }

    private string? GetDuplicateBlocker()
    {
        var targets = this.DescribeSelection();
        return relocation is null ? UnavailableReason : targets.Count == 0 ? "Select items to duplicate."
            : targets.Any(static target => target.IsImportedOutput) ? "Imported assets belong to their model. Import the model again instead."
            : targets.Any(static target => target.VirtualPath is null) ? ReadOnlyReason
            : targets.Any(static target => target.IsEditorScene) && targets.Count != 1 ? "Duplicate one scene at a time."
            : targets.Any(static target => !target.IsEditorScene && target.ModelSourcePath is not null) ? "Import the model again instead of duplicating it."
            : null;
    }

    private string? GetDeleteBlocker()
    {
        var targets = this.DescribeSelection();
        return relocation is null ? UnavailableReason : targets.Count == 0 ? "Select items to delete."
            : targets.Any(static target => target.IsImportedOutput) ? "Imported assets belong to their model. Delete the model instead."
            : targets.Any(static target => target.VirtualPath is null) ? ReadOnlyReason
            : targets.Any(static target => target.IsEditorScene) && targets.Count != 1 ? "Delete one scene at a time."
            : null;
    }

    private void OnRelocationCompleted(object? sender, AssetRelocationCompletedEventArgs e)
    {
        var outcome = e.Outcome;
        this.OperationResultTitle = outcome.Title;
        this.OperationResultMessage = outcome.Message;
        this.OperationResultSeverity = outcome.Succeeded ? InfoBarSeverity.Success : InfoBarSeverity.Error;
        this.IsUndoAvailable = outcome.CanUndo && relocation?.CanUndo == true;
        this.IsOperationResultVisible = true;
        this.NotifyAssetActions();
    }

    private bool CanOpenSelected() => this.SelectedAssets.Count == 1;

    [RelayCommand(CanExecute = nameof(CanOpenSelected))]
    private void OpenSelected()
    {
        if (this.CanOpenSelected() && this.LayoutViewModel is AssetsLayoutViewModel layout)
        {
            layout.Invoke(this.SelectedAssets[0]);
        }
    }

    private bool CanRenameSelected() => this.GetRenameBlocker() is null;

    [RelayCommand(CanExecute = nameof(CanRenameSelected))]
    private async Task RenameSelectedAsync()
    {
        if (relocation is null || !this.CanRenameSelected())
        {
            return;
        }

        var target = this.DescribeSelection()[0];
        var name = RelocationPaths.GetName(target.VirtualPath!);
        if (target.IsEditorScene)
        {
            var scene = RelocationPaths.GetDisplayName(name);
            if (await relocation.PromptNameAsync("Rename scene", scene).ConfigureAwait(true) is { } newScene)
            {
                _ = await relocation.RunSceneCommandAsync(SceneAssetCommand.Rename, scene, newScene).ConfigureAwait(true);
            }

            return;
        }

        var current = target.IsFolder ? name : RelocationPaths.GetDisplayName(name);
        if (await relocation.PromptNameAsync("Rename", current).ConfigureAwait(true) is { } newName)
        {
            var request = new AssetRelocationRequest { Moves = [new(target.VirtualPath!, RelocationPaths.Rename(target.VirtualPath!, newName, target.IsFolder))] };
            _ = await relocation.RelocateAsync(request, "Rename").ConfigureAwait(true);
        }
    }

    private bool CanRenameOutputGroup()
        => relocation is not null && this.DescribeSelection() is [{ ModelSourcePath: { } model }]
            && File.Exists(model + NativeSceneImportSettings.SidecarSuffix);

    [RelayCommand(CanExecute = nameof(CanRenameOutputGroup))]
    private async Task RenameOutputGroupAsync()
    {
        if (relocation is null || !this.CanRenameOutputGroup() || projectContextService.ActiveProject is not { } project)
        {
            return;
        }

        var model = this.DescribeSelection()[0].ModelSourcePath!;
        NativeSceneImportSettings settings;
        try
        {
            settings = NativeSceneImportSettings.Parse(await File.ReadAllBytesAsync(model + NativeSceneImportSettings.SidecarSuffix).ConfigureAwait(true));
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or InvalidDataException or System.Text.Json.JsonException)
        {
            this.OnRelocationCompleted(this, new(new(Succeeded: false, "Rename output group", error.Message, relocation.CanUndo)));
            return;
        }

        if (RelocationPaths.ToVirtual(project, model) is { } source
            && await relocation.PromptNameAsync("Rename output group", settings.OutputDirectory, allowSeparators: true).ConfigureAwait(true) is { } group)
        {
            _ = await relocation.RelocateAsync(new AssetRelocationRequest { GroupMoves = [new(source, group)] }, "Rename output group").ConfigureAwait(true);
        }
    }

    private bool CanTransferSelected() => this.GetTransferBlocker() is null;

    [RelayCommand(CanExecute = nameof(CanTransferSelected))]
    private void CutSelected() => this.SetClipboard(isCut: true);

    [RelayCommand(CanExecute = nameof(CanTransferSelected))]
    private void CopySelected() => this.SetClipboard(isCut: false);

    private void SetClipboard(bool isCut)
    {
        if (!this.CanTransferSelected())
        {
            return;
        }

        this.clipboard = [.. this.DescribeSelection().Select(static target => target.VirtualPath!)];
        this.clipboardIsCut = isCut;
        this.NotifyAssetActions();
    }

    private bool CanPaste() => relocation is not null && this.clipboard.Count != 0 && this.IsSelectedFolderWritable();

    [RelayCommand(CanExecute = nameof(CanPaste))]
    private async Task PasteAsync()
    {
        if (relocation is null || !this.CanPaste())
        {
            return;
        }

        var folder = Uri.UnescapeDataString(this.GetSelectedFolderUri().AbsolutePath).TrimEnd('/');
        if (!this.clipboardIsCut)
        {
            _ = await relocation.CopyAsync(this.clipboard, folder, "Paste").ConfigureAwait(true);
            return;
        }

        var moves = this.clipboard.Where(path => !string.Equals(RelocationPaths.GetParent(path), folder, StringComparison.OrdinalIgnoreCase))
            .Select(path => new AssetRelocationMove(path, RelocationPaths.Combine(folder, RelocationPaths.GetName(path)))).ToArray();
        if (moves.Length == 0)
        {
            return;
        }

        if (await relocation.RelocateAsync(new AssetRelocationRequest { Moves = moves }, "Move").ConfigureAwait(true) is { Succeeded: true })
        {
            this.clipboard = [];
            this.NotifyAssetActions();
        }
    }

    private bool CanDuplicateSelected() => this.GetDuplicateBlocker() is null;

    [RelayCommand(CanExecute = nameof(CanDuplicateSelected))]
    private async Task DuplicateSelectedAsync()
    {
        if (relocation is null || !this.CanDuplicateSelected())
        {
            return;
        }

        var targets = this.DescribeSelection();
        if (targets is [{ IsEditorScene: true } scene])
        {
            var name = RelocationPaths.GetDisplayName(RelocationPaths.GetName(scene.VirtualPath!));
            _ = await relocation.RunSceneCommandAsync(SceneAssetCommand.Duplicate, name, newName: null).ConfigureAwait(true);
            return;
        }

        foreach (var group in targets.GroupBy(static target => RelocationPaths.GetParent(target.VirtualPath!), StringComparer.OrdinalIgnoreCase))
        {
            if (await relocation.CopyAsync([.. group.Select(static target => target.VirtualPath!)], group.Key, "Duplicate").ConfigureAwait(true) is { Succeeded: false })
            {
                return;
            }
        }
    }

    private bool CanDeleteSelected() => this.GetDeleteBlocker() is null;

    [RelayCommand(CanExecute = nameof(CanDeleteSelected))]
    private async Task DeleteSelectedAsync()
    {
        if (relocation is null || !this.CanDeleteSelected())
        {
            return;
        }

        var targets = this.DescribeSelection();
        if (targets is [{ IsEditorScene: true } scene])
        {
            var name = RelocationPaths.GetDisplayName(RelocationPaths.GetName(scene.VirtualPath!));
            if (await dialogService.ConfirmAsync("Delete scene", $"Move scene '{name}' to the Recycle Bin?").ConfigureAwait(true))
            {
                _ = await relocation.RunSceneCommandAsync(SceneAssetCommand.Delete, name, newName: null).ConfigureAwait(true);
            }

            return;
        }

        _ = await relocation.DeleteAsync([.. targets.Select(static target => target.VirtualPath!)]).ConfigureAwait(true);
    }

    private bool CanFindReferences() => relocation is not null && this.DescribeSelection() is [{ VirtualPath: not null }];

    [RelayCommand(CanExecute = nameof(CanFindReferences))]
    private void FindReferences()
    {
        if (this.CanFindReferences())
        {
            this.Presentation.IsDetailsPaneOpen = true;
        }
    }

    private bool CanUndoRelocation() => relocation?.CanUndo == true;

    [RelayCommand(CanExecute = nameof(CanUndoRelocation))]
    private async Task UndoRelocationAsync()
    {
        if (relocation is not null)
        {
            this.IsUndoAvailable = false;
            _ = await relocation.UndoAsync().ConfigureAwait(true);
        }
    }

    private bool CanCopyPath() => this.SelectedAssets.Count > 0;

    [RelayCommand(CanExecute = nameof(CanCopyPath))]
    private void CopyPath()
    {
        if (this.CanCopyPath())
        {
            shell.CopyText(GetCopyPathText(this.SelectedAssets));
        }
    }

    private bool CanShowInFileExplorer() => this.SelectedAssets is [var asset] && AssetFileFacts.GetPath(asset) is not null;

    [RelayCommand(CanExecute = nameof(CanShowInFileExplorer))]
    private async Task ShowInFileExplorerAsync()
    {
        if (this.SelectedAssets is [var asset] && AssetFileFacts.GetPath(asset) is { } path && !shell.ShowInFileExplorer(path))
        {
            await dialogService.ShowMessageAsync("Show in File Explorer", $"'{path}' no longer exists.").ConfigureAwait(true);
        }
    }

    [RelayCommand]
    private void ShowCookingJobs() => _ = messenger.Send(new ShowCookingJobsRequestMessage());

    /// <summary>What a selected row is on disk, for deciding which commands apply.</summary>
    /// <param name="VirtualPath">The writable authoring path, or null for read-only and cooked-only rows.</param>
    /// <param name="PhysicalPath">The file or folder.</param>
    /// <param name="IsFolder">Whether the row is a folder.</param>
    /// <param name="IsEditorScene">Whether the row is one of the project's named scenes.</param>
    /// <param name="IsImportedOutput">Whether the row is a cooked-only output of a model import.</param>
    /// <param name="ModelSourcePath">The model source the row is, or was imported from.</param>
    private readonly record struct AssetTarget(string? VirtualPath, string? PhysicalPath, bool IsFolder, bool IsEditorScene, bool IsImportedOutput, string? ModelSourcePath);
}
