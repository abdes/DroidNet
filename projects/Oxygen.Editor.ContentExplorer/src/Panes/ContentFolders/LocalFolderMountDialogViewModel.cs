// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Aura.Dialogs;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>
///     ViewModel for the Local Folder mount dialog.
/// </summary>
public sealed partial class LocalFolderMountDialogViewModel : ObservableObject
{
    private readonly IDialogService dialogService;
    private readonly HashSet<string> existingMountPointNames;

    public LocalFolderMountDialogViewModel(
        IDialogService dialogService,
        IReadOnlyCollection<string> existingMountPointNames,
        FileSystemPath projectRoot)
    {
        this.dialogService = dialogService;
        this.Placement = new(projectRoot);
        this.existingMountPointNames = new HashSet<string>(existingMountPointNames, StringComparer.Ordinal);
        this.Revalidate();
    }

    /// <summary>Gets where the folder is saved: in <c>Project.oxy</c>, or on this machine only.</summary>
    public MountPlacement Placement { get; }

    [ObservableProperty]
    public partial string MountPointName { get; set; } = string.Empty;

    [ObservableProperty]
    public partial string SelectedFolderPath { get; set; } = string.Empty;

    [ObservableProperty]
    public partial string ErrorMessage { get; set; } = string.Empty;

    [ObservableProperty]
    public partial bool CanAccept { get; set; }

    /// <summary>
    ///     Gets the resulting mount definition when the dialog is accepted.
    /// </summary>
    public LocalFolderMountDefinition? Result
        => this.CanAccept
            ? new LocalFolderMountDefinition(this.MountPointName.Trim(), this.SelectedFolderPath, this.Placement.CommittedPath)
            : null;

    [RelayCommand]
    private async Task BrowseAsync(CancellationToken cancellationToken)
    {
        try
        {
            var picker = new FolderPickerSpec("Choose local folder mount", "Oxygen.LocalFolderMount")
            {
                SuggestedStartFolder = this.SelectedFolderPath,
            };
            var folder = await this.dialogService.PickFolderAsync(picker, cancellationToken).ConfigureAwait(true);
            if (!string.IsNullOrEmpty(folder))
            {
                this.SelectedFolderPath = folder;
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException
            or InvalidOperationException or System.Runtime.InteropServices.COMException)
        {
            this.ErrorMessage = $"Could not pick folder: {ex.Message}";
        }
    }

    partial void OnMountPointNameChanged(string value) => this.Revalidate();

    partial void OnSelectedFolderPathChanged(string value)
    {
        this.Placement.Location = value;
        this.Revalidate();
    }

    private void Revalidate()
    {
        var name = this.MountPointName.Trim();

        if (!string.IsNullOrEmpty(name) && this.existingMountPointNames.Contains(name))
        {
            this.ErrorMessage = "A mount point with this name already exists.";
        }
        else
        {
            this.ErrorMessage = string.Empty;
        }

        this.CanAccept = !string.IsNullOrEmpty(name)
            && !string.IsNullOrEmpty(this.SelectedFolderPath)
            && string.IsNullOrEmpty(this.ErrorMessage);
    }
}
