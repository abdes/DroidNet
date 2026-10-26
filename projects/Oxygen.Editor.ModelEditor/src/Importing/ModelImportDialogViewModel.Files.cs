// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;
using DroidNet.Aura.Dialogs;
using Oxygen.Editor.Models;
using Oxygen.Managed.Assets;

namespace Oxygen.Editor.ModelEditor.Importing;

/// <summary>The file list: add, remove and reorder files, locate a missing one, and analyze each added file.</summary>
public sealed partial class ModelImportDialogViewModel
{
    private static readonly IReadOnlyDictionary<string, IReadOnlyList<string>> ModelFileTypes
        = new Dictionary<string, IReadOnlyList<string>>(StringComparer.Ordinal)
        {
            ["3D models"] = [.. AssetFileTypes.Suffixes(AssetFileCategory.Model)],
        };

    /// <summary>Adds model files to the end of the list, each with a name from its file and the dialog's folder, and
    /// analyzes them. A file already listed, or not a model, is left out.</summary>
    /// <param name="paths">The files, such as the picker's or those dropped from File Explorer.</param>
    /// <returns>The files added.</returns>
    public IReadOnlyList<ModelImportFile> AddFiles(IEnumerable<string> paths)
    {
        ArgumentNullException.ThrowIfNull(paths);
        var listed = this.Files.Select(static file => file.SourcePath).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var records = new ModelImportRecords(this.project.FileSystem, this.project.ProjectRoot.FullPath);
        var added = new List<ModelImportFile>();
        var ignored = 0;
        foreach (var path in paths.Select(Path.GetFullPath))
        {
            if (!AssetFileTypes.Is(path, AssetFileCategory.Model))
            {
                ++ignored;
                continue;
            }

            if (listed.Add(path))
            {
                // A model already imported in place starts from the settings of its last import, which it imports again.
                var name = Path.GetFileNameWithoutExtension(path);
                var settings = this.ProjectSourceIdentity(path) is not null && records.TryFindBySource(path, out var record)
                    ? record.Settings.Settings.Clone()
                    : NewSettings(name);
                added.Add(this.Append(new ModelImportFile(path, name, this.destination, settings)));
            }
        }

        this.NoticeText = ignored == 0 ? string.Empty : $"{ignored} file(s) that are not models were left out.";
        if (added.Count != 0)
        {
            this.RequestSelection(added);
        }

        return added;
    }

    private ModelImportFile Append(ModelImportFile file)
    {
        this.Files.Add(file);
        _ = this.AnalyzeAsync(file);
        return file;
    }

    // Reads what the file holds; the dialog fills as each analysis completes, at most two at a time.
    private async Task AnalyzeAsync(ModelImportFile file)
    {
        if (!this.project.FileSystem.File.Exists(file.SourcePath))
        {
            return;
        }

        var token = this.lifetime.Token;
        file.IsAnalyzing = true;
        this.RefreshIfSelected(file);
        try
        {
            await this.analyses.WaitAsync(token).ConfigureAwait(true);
            try
            {
                file.Analysis = await this.analyzer.AnalyzeAsync(this.project, file.SourcePath, token).ConfigureAwait(true);
            }
            finally
            {
                _ = this.analyses.Release();
            }
        }
        catch (OperationCanceledException)
        {
            // The dialog closed.
            return;
        }
        catch (ObjectDisposedException)
        {
            // The dialog closed while the analysis waited.
            return;
        }

        file.IsAnalyzing = false;
        this.RefreshIfSelected(file);
    }

    private void RefreshIfSelected(ModelImportFile file)
    {
        if (!this.disposed && this.selection.Contains(file))
        {
            this.Settings.Show(this.selection);
            this.RefreshSelectionText();
        }
    }

    private void NotifyListCommands()
    {
        this.RemoveCommand.NotifyCanExecuteChanged();
        this.MoveUpCommand.NotifyCanExecuteChanged();
        this.MoveDownCommand.NotifyCanExecuteChanged();
        this.LocateCommand.NotifyCanExecuteChanged();
        this.RevealCommand.NotifyCanExecuteChanged();
        this.SaveSessionCommand.NotifyCanExecuteChanged();
        this.BrowseFolderCommand.NotifyCanExecuteChanged();
    }

    [RelayCommand]
    private async Task AddFilesAsync(CancellationToken cancellationToken)
    {
        try
        {
            var picker = new FilePickerSpec("Add model files", "Oxygen.ImportModel", ModelFileTypes)
            {
                SuggestedStartFolder = this.project.ProjectRoot.FullPath,
            };
            _ = this.AddFiles(await this.dialogs.PickFilesAsync(picker, cancellationToken).ConfigureAwait(true));
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            // Cancelling the picker keeps the list.
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or ArgumentException or System.Runtime.InteropServices.COMException)
        {
            this.ErrorText = "Could not add the files. " + error.Message;
        }
    }

    private bool CanRemove() => this.HasSelection;

    [RelayCommand(CanExecute = nameof(CanRemove))]
    private void Remove()
    {
        // The selection is cleared first: each removal refreshes the list commands, which read the selection.
        var first = this.Files.IndexOf(this.selection[0]);
        var removed = this.selection.ToArray();
        this.UpdateSelection([]);
        foreach (var file in removed)
        {
            _ = this.Files.Remove(file);
        }

        if (this.Files.Count != 0)
        {
            this.RequestSelection([this.Files[Math.Min(first, this.Files.Count - 1)]]);
        }
    }

    private bool CanMoveUp() => this.HasSelection && !ReferenceEquals(this.selection[0], this.Files[0]);

    [RelayCommand(CanExecute = nameof(CanMoveUp))]
    private void MoveUp() => this.MoveSelection(-1);

    private bool CanMoveDown() => this.HasSelection && !ReferenceEquals(this.selection[^1], this.Files[^1]);

    [RelayCommand(CanExecute = nameof(CanMoveDown))]
    private void MoveDown() => this.MoveSelection(1);

    // Moves every selected file one place, the list's order being the run's and the session's; the selection stays.
    private void MoveSelection(int step)
    {
        var moved = this.selection.ToArray();
        var ordered = step < 0 ? moved : Enumerable.Reverse(moved);
        foreach (var file in ordered)
        {
            var index = this.Files.IndexOf(file);
            this.Files.Move(index, index + step);
        }

        this.RequestSelection(moved);
    }

    private bool CanLocate() => this.IsSingleSelection && this.selection[0].IsMissing;

    // Points a missing file at its new place, keeping its name, folder and settings.
    [RelayCommand(CanExecute = nameof(CanLocate))]
    private async Task LocateAsync(CancellationToken cancellationToken)
    {
        var file = this.selection[0];
        try
        {
            var picker = new FilePickerSpec($"Locate {file.FileName}", "Oxygen.ImportModel", ModelFileTypes)
            {
                SuggestedStartFolder = this.project.ProjectRoot.FullPath,
            };
            if (await this.dialogs.PickFileAsync(picker, cancellationToken).ConfigureAwait(true) is { } located)
            {
                file.SourcePath = Path.GetFullPath(located);
                file.Analysis = null;
                this.CheckFiles();
                _ = this.AnalyzeAsync(file);
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            // Cancelling the picker keeps the missing file.
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or ArgumentException or System.Runtime.InteropServices.COMException)
        {
            this.ErrorText = "Could not locate the file. " + error.Message;
        }
    }

    private bool CanReveal() => this.IsSingleSelection && !this.selection[0].IsMissing && this.shell is not null;

    [RelayCommand(CanExecute = nameof(CanReveal))]
    private void Reveal() => _ = this.shell?.ShowInFileExplorer(this.selection[0].SourcePath);
}
