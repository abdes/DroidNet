// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Text.Json;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Aura.Dialogs;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.Models;

namespace Oxygen.Editor.ModelEditor.Importing;

/// <summary>Replacements and import sessions: a session is an ImportTool import manifest, which the editor and
/// the command line run alike.</summary>
public sealed partial class ModelImportDialogViewModel
{
    private static readonly IReadOnlyDictionary<string, IReadOnlyList<string>> SessionFileTypes
        = new Dictionary<string, IReadOnlyList<string>>(StringComparer.Ordinal)
        {
            ["Import session"] = [".json"],
        };

    private static bool HasNoSettings(JsonElement settings)
        => settings.ValueKind != JsonValueKind.Object
            || settings.EnumerateObject().All(static property => string.Equals(property.Name, ModelImportSettingCatalog.NameKey, StringComparison.Ordinal));

    /// <summary>Adds an import session's models to the list and takes its run settings.</summary>
    /// <param name="manifestPath">The session's import manifest.</param>
    /// <param name="cancellationToken">Cancels reading it.</param>
    /// <returns>The files added; a model whose source is missing is listed with its error.</returns>
    public async Task<IReadOnlyList<ModelImportFile>> OpenSessionFileAsync(string manifestPath, CancellationToken cancellationToken)
    {
        var bytes = await this.project.FileSystem.File.ReadAllBytesAsync(manifestPath, cancellationToken).ConfigureAwait(true);
        var session = ModelImportSession.Read(manifestPath, bytes);
        var listed = this.Files.Select(static file => file.SourcePath).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var added = session.Jobs.Where(job => listed.Add(job.Source))
            .Select(job => this.Append(new ModelImportFile(job.Source, job.Name, this.FolderOf(job), job.Settings)))
            .ToArray();
        this.Run.Load(session.Run);
        this.NoticeText = session.SkippedJobs.IsEmpty
            ? string.Empty
            : string.Create(CultureInfo.CurrentCulture, $"{session.SkippedJobs.Length} job(s) of the session are not models and were left out.");
        if (added.Length != 0)
        {
            this.RequestSelection(added);
        }

        return added;
    }

    /// <summary>Saves the list and the run's settings as an import session.</summary>
    /// <param name="manifestPath">Where the session's import manifest goes.</param>
    /// <param name="cancellationToken">Cancels writing it.</param>
    /// <returns>A task that completes when the session is written.</returns>
    public Task SaveSessionFileAsync(string manifestPath, CancellationToken cancellationToken)
    {
        var session = new ModelImportSession
        {
            Jobs = [.. this.Files.Select(file => new ModelImportSessionJob(file.SourcePath, this.PlannedIdentity(file), file.SettingsWithName()))],
            Run = this.Run.ToSettings(),
        };
        return this.project.FileSystem.File.WriteAllBytesAsync(manifestPath, session.ToBytes(manifestPath), cancellationToken);
    }

    // A session's model goes back to the folder its identity names: the folder of a project source, or the folder that
    // holds the model's own folder.
    private string FolderOf(ModelImportSessionJob job)
        => (this.ProjectSourceIdentity(job.Source) is not null ? job.VirtualPath.Parent : job.VirtualPath.Parent?.Parent)?.VirtualPath
            ?? this.destination;

    private bool CanReplace() => this.HasNamesToReplace;

    // Replaces, on the person's word, every selected model whose name is taken: the existing model must be one model
    // of its own folder, it keeps its record, and a file that has no settings of its own yet starts from the record's.
    [RelayCommand(CanExecute = nameof(CanReplace))]
    private async Task ReplaceAsync(CancellationToken cancellationToken)
    {
        this.ErrorText = string.Empty;
        foreach (var file in this.selection.Where(static file => file.IsNameTaken && file.Replacement is null).ToArray())
        {
            try
            {
                var replacement = await SceneImportReplacement.ReviewAsync(this.project, ImportDestination.Parse(file.DestinationFolder), file.Name.Trim(), cancellationToken).ConfigureAwait(true);
                file.Replacement = replacement;
                if (HasNoSettings(file.Settings))
                {
                    file.Settings = replacement.Record.Settings.Settings.Clone();
                }
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            catch (Exception error) when (error is IOException or InvalidDataException or UnauthorizedAccessException or ArgumentException or JsonException or InvalidOperationException)
            {
                this.ErrorText = $"{file.FileName}: {error.Message}";
            }
        }

        this.CheckFiles();
        this.Settings.Show(this.selection);
        this.RefreshSelectionText();
    }

    [RelayCommand]
    private async Task OpenSessionAsync(CancellationToken cancellationToken)
    {
        try
        {
            var picker = new FilePickerSpec("Open import session", "Oxygen.ImportSession", SessionFileTypes)
            {
                SuggestedStartFolder = this.project.ProjectRoot.FullPath,
            };
            if (await this.dialogs.PickFileAsync(picker, cancellationToken).ConfigureAwait(true) is { } path)
            {
                this.ErrorText = string.Empty;
                _ = await this.OpenSessionFileAsync(path, cancellationToken).ConfigureAwait(true);
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            // Cancelling keeps the list.
        }
        catch (Exception error) when (error is IOException or InvalidDataException or UnauthorizedAccessException or ArgumentException or System.Runtime.InteropServices.COMException)
        {
            this.ErrorText = "Could not open the import session. " + error.Message;
        }
    }

    private bool CanSaveSession() => !this.IsEmpty;

    [RelayCommand(CanExecute = nameof(CanSaveSession))]
    private async Task SaveSessionAsync(CancellationToken cancellationToken)
    {
        try
        {
            var picker = new FileSavePickerSpec("Save import session", "Oxygen.ImportSession", SessionFileTypes)
            {
                SuggestedStartFolder = this.project.ProjectRoot.FullPath,
                SuggestedFileName = this.project.Name + ".import-manifest.json",
            };
            if (await this.dialogs.PickSaveFileAsync(picker, cancellationToken).ConfigureAwait(true) is { } path)
            {
                this.ErrorText = string.Empty;
                await this.SaveSessionFileAsync(path, cancellationToken).ConfigureAwait(true);
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            // Cancelling saves nothing.
        }
        catch (Exception error) when (error is IOException or InvalidDataException or UnauthorizedAccessException or ArgumentException or System.Runtime.InteropServices.COMException)
        {
            this.ErrorText = "Could not save the import session. " + error.Message;
        }
    }
}
