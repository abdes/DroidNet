// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;
using DroidNet.Aura.Dialogs;

namespace Oxygen.Editor.ModelEditor.Importing;

/// <summary>The content folder of the selected files, chosen from the project's authoring mounts.</summary>
public sealed partial class ModelImportDialogViewModel
{
    /// <summary>Gets the content folder of a folder on disk: its path under the authoring mount that holds it.</summary>
    /// <param name="folder">A folder on disk.</param>
    /// <returns>The content folder, such as <c>/Content/Vehicles</c>, or null outside every authoring mount.</returns>
    public string? ContentFolderOf(string folder)
    {
        ArgumentException.ThrowIfNullOrEmpty(folder);
        foreach (var mount in this.project.AuthoringMounts)
        {
            var root = this.project.ProjectRoot.Combine(mount.RelativePath).FullPath;
            var relative = Path.GetRelativePath(root, folder).Replace('\\', '/');
            if (!Path.IsPathRooted(relative) && !string.Equals(relative, "..", StringComparison.Ordinal) && !relative.StartsWith("../", StringComparison.Ordinal))
            {
                return "/" + mount.Name + (string.Equals(relative, ".", StringComparison.Ordinal) ? string.Empty : "/" + relative);
            }
        }

        return null;
    }

    [RelayCommand(CanExecute = nameof(HasSelection))]
    private async Task BrowseFolderAsync(CancellationToken cancellationToken)
    {
        try
        {
            var picker = new FolderPickerSpec("Choose the models' content folder", "Oxygen.ModelDestination")
            {
                SuggestedStartFolder = this.project.ProjectRoot.FullPath,
            };
            if (await this.dialogs.PickFolderAsync(picker, cancellationToken).ConfigureAwait(true) is not { } selected)
            {
                return;
            }

            if (this.ContentFolderOf(selected) is { } folder)
            {
                this.ErrorText = string.Empty;
                this.SelectedFolder = folder;
            }
            else
            {
                this.ErrorText = "Choose a folder in one of this project's authoring mounts.";
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            // Cancelling the picker keeps the folder.
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or ArgumentException or System.Runtime.InteropServices.COMException)
        {
            this.ErrorText = "Could not choose the folder. " + error.Message;
        }
    }
}
