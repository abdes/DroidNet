// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Dialogs;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>Asks for the folder, or the pak file, a mount is bound to: in the Content mounts dialog and in the tree.</summary>
internal static class MountLocationPicker
{
    /// <summary>The extension of a pak file.</summary>
    internal const string PakExtension = ".pak";

    /// <summary>Asks for a folder, or a pak file.</summary>
    /// <param name="dialogService">The pickers.</param>
    /// <param name="pakFile">Whether a pak file is picked rather than a folder.</param>
    /// <param name="start">Where the picker starts, if anywhere.</param>
    /// <param name="cancellationToken">Cancels the picker.</param>
    /// <returns>The picked location, full; <see langword="null"/> when none was picked.</returns>
    /// <exception cref="IOException">The picker failed, with the reason in its message.</exception>
    public static async Task<string?> PickAsync(IDialogService dialogService, bool pakFile, string? start, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(dialogService);
        try
        {
            var path = pakFile
                ? await dialogService.PickFileAsync(
                    new FilePickerSpec("Choose a pak file", "Oxygen.PakFileMount", new Dictionary<string, IReadOnlyList<string>>(StringComparer.Ordinal) { ["Pak files"] = [PakExtension] })
                    {
                        SuggestedStartFolder = start,
                    },
                    cancellationToken).ConfigureAwait(true)
                : await dialogService.PickFolderAsync(
                    new FolderPickerSpec("Choose a folder", "Oxygen.LocalFolderMount") { SuggestedStartFolder = start },
                    cancellationToken).ConfigureAwait(true);
            return path is { Length: > 0 } ? Path.GetFullPath(path) : null;
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            // The dialog closed while the picker was open.
            return null;
        }
        catch (Exception error) when (error is UnauthorizedAccessException or ArgumentException
            or InvalidOperationException or System.Runtime.InteropServices.COMException)
        {
            throw new IOException($"Could not pick a {(pakFile ? "pak file" : "folder")}: {error.Message}", error);
        }
    }
}
