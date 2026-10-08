// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentBrowser;

/// <summary>Commands that act on the selected assets: open, paths, File Explorer and the cooking jobs.</summary>
public partial class AssetsViewModel
{
    /// <summary>
    /// The reason rename, move, copy, duplicate and delete are unavailable: each changes asset identity, and every
    /// authored reference must move with it in one transaction.
    /// </summary>
    public const string RelocationUnavailableReason
        = "Not available yet: the scenes and materials that use an asset must be updated with it.";

    /// <summary>Gets the Rename tooltip, with why it is unavailable.</summary>
    public static string RenameToolTip { get; } = DescribeUnavailable("Rename (F2)");

    /// <summary>Gets the Cut tooltip, with why it is unavailable.</summary>
    public static string CutToolTip { get; } = DescribeUnavailable("Cut (Ctrl+X)");

    /// <summary>Gets the Copy tooltip, with why it is unavailable.</summary>
    public static string CopyToolTip { get; } = DescribeUnavailable("Copy (Ctrl+C)");

    /// <summary>Gets the Paste tooltip, with why it is unavailable.</summary>
    public static string PasteToolTip { get; } = DescribeUnavailable("Paste (Ctrl+V)");

    /// <summary>Gets the Duplicate tooltip, with why it is unavailable.</summary>
    public static string DuplicateToolTip { get; } = DescribeUnavailable("Duplicate (Ctrl+D)");

    /// <summary>Gets the Delete tooltip, with why it is unavailable.</summary>
    public static string DeleteToolTip { get; } = DescribeUnavailable("Delete (Del)");

    /// <summary>
    /// Gets the command behind Rename, Cut, Copy, Paste, Duplicate and Delete. It stays unavailable, with
    /// <see cref="RelocationUnavailableReason"/> shown on each, until relocation updates references.
    /// </summary>
    public IRelayCommand RelocationCommand { get; } = new RelayCommand(static () => { }, static () => false);

    /// <summary>Gets the selected assets of the active layout; the last is the active asset.</summary>
    public IReadOnlyList<ContentBrowserAssetItem> SelectedAssets => contentBrowserState.SelectedAssets;

    /// <summary>Gets the shared result order, view and tile size.</summary>
    public AssetBrowserPresentation Presentation => contentBrowserState.Presentation;

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

    private static string DescribeUnavailable(string command) => command + Environment.NewLine + RelocationUnavailableReason;

    private void NotifyAssetActions()
    {
        this.OnPropertyChanged(nameof(this.SelectedAssets));
        this.OpenSelectedCommand.NotifyCanExecuteChanged();
        this.CopyPathCommand.NotifyCanExecuteChanged();
        this.ShowInFileExplorerCommand.NotifyCanExecuteChanged();
        foreach (var name in (string[])[
            nameof(this.CreateFolderToolTip),
            nameof(this.CookSelectedToolTip),
            nameof(this.CookFolderToolTip),
            nameof(this.ReimportToolTip),
            nameof(this.ShowImportSourceToolTip),
            nameof(this.OpenToolTip),
            nameof(this.CopyPathToolTip),
            nameof(this.ShowInFileExplorerToolTip),
        ])
        {
            this.OnPropertyChanged(name);
        }
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
}
