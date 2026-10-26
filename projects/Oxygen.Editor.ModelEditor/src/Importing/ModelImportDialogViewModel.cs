// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Collections.ObjectModel;
using System.Globalization;
using System.Text.Json;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Aura.Dialogs;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ModelEditor.Settings;
using Oxygen.Editor.Models;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ModelEditor.Importing;

/// <summary>The Import model dialog: a list of model files edited as a selection, imported as one run with its
/// execution settings, and saved or opened as an import session.</summary>
public sealed partial class ModelImportDialogViewModel : ObservableObject, IDisposable
{
    private readonly ProjectContext project;
    private readonly IDialogService dialogs;
    private readonly IModelSourceAnalyzer analyzer;
    private readonly IDesktopIntegration? shell;
    private readonly string destination;
    private readonly CancellationTokenSource lifetime = new();
    private readonly SemaphoreSlim analyses = new(2, 2);
    private IReadOnlyList<ModelImportFile> selection = [];
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="ModelImportDialogViewModel"/> class.</summary>
    /// <param name="project">The project the models are imported into.</param>
    /// <param name="dialogs">The file pickers and messages.</param>
    /// <param name="analyzer">Analyzes each added file.</param>
    /// <param name="destination">The folder new models go into.</param>
    /// <param name="runDefaults">The ImportTool's execution defaults.</param>
    /// <param name="shell">Reveals a file in File Explorer; without it Reveal is unavailable.</param>
    public ModelImportDialogViewModel(ProjectContext project, IDialogService dialogs, IModelSourceAnalyzer analyzer, AssetPath destination, ImportRunSettings runDefaults, IDesktopIntegration? shell = null)
    {
        ArgumentNullException.ThrowIfNull(destination);
        this.project = project ?? throw new ArgumentNullException(nameof(project));
        this.dialogs = dialogs ?? throw new ArgumentNullException(nameof(dialogs));
        this.analyzer = analyzer ?? throw new ArgumentNullException(nameof(analyzer));
        this.destination = destination.VirtualPath;
        this.shell = shell;
        this.Run = new(runDefaults);
        this.Run.PropertyChanged += this.OnRunChanged;
        this.Files.CollectionChanged += (_, _) => this.OnFilesChanged();
    }

    /// <summary>Occurs when the dialog selects files itself, after files are added, removed or moved, so the list shows
    /// that selection; <see cref="Selection"/> already holds it.</summary>
    public event EventHandler<IReadOnlyList<ModelImportFile>>? SelectionRequested;

    /// <summary>Gets the files, in the order of the run's jobs and of a saved session.</summary>
    public ObservableCollection<ModelImportFile> Files { get; } = [];

    /// <summary>Gets the selected files, in the list's order.</summary>
    public IReadOnlyList<ModelImportFile> Selection => this.selection;

    /// <summary>Gets the import settings panel, showing the selected files.</summary>
    public ModelImportSettingsPanelViewModel Settings { get; } = new();

    /// <summary>Gets the run's execution settings.</summary>
    public ImportRunViewModel Run { get; }

    /// <summary>Gets or sets the Before import groups of the selected files that have findings.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasReport))]
    [NotifyPropertyChangedFor(nameof(ReportCount))]
    [NotifyPropertyChangedFor(nameof(ReportSeverity))]
    [NotifyPropertyChangedFor(nameof(ReportSummary))]
    public partial IReadOnlyList<BeforeImportGroup> BeforeImport { get; set; } = [];

    /// <summary>Gets or sets the facts line of the selected file, or how many files are selected.</summary>
    [ObservableProperty]
    public partial string Facts { get; set; } = string.Empty;

    /// <summary>Gets or sets a message about the last command that failed, such as a session that could not be read.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasError))]
    public partial string ErrorText { get; set; } = string.Empty;

    /// <summary>Gets or sets a note about the last command, such as session jobs that were left out.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasNotice))]
    public partial string NoticeText { get; set; } = string.Empty;

    /// <summary>Gets or sets a value indicating whether every file can be imported.</summary>
    [ObservableProperty]
    public partial bool CanImport { get; set; }

    /// <summary>Gets a value indicating whether the selected files have findings, when the Before import button shows.</summary>
    public bool HasReport => this.ReportCount != 0;

    /// <summary>Gets how many findings the selected files have.</summary>
    public int ReportCount => this.BeforeImport.Sum(static group => group.Items.Length);

    /// <summary>Gets the most serious finding's severity, the facts bar's severity; informational without findings.</summary>
    public DiagnosticSeverity ReportSeverity
        => this.BeforeImport.SelectMany(static group => group.Items).Select(static item => item.Severity).DefaultIfEmpty(DiagnosticSeverity.Info).Max();

    /// <summary>Gets how many errors and warnings the findings hold, the facts bar's title, such as "1 error, 2 warnings";
    /// empty without findings.</summary>
    public string ReportSummary
    {
        get
        {
            var findings = this.BeforeImport.SelectMany(static group => group.Items).ToArray();
            var errors = findings.Count(static item => item.Severity >= DiagnosticSeverity.Error);
            var warnings = findings.Length - errors;
            return string.Join(", ", new[] { Count(errors, "error"), Count(warnings, "warning") }.Where(static part => part.Length != 0));

            static string Count(int count, string noun)
                => count switch
                {
                    0 => string.Empty,
                    1 => $"1 {noun}",
                    _ => string.Create(CultureInfo.CurrentCulture, $"{count} {noun}s"),
                };
        }
    }

    /// <summary>Gets what blocks the import of the selected files, one line each, such as a taken name or a missing
    /// source; with several files selected, each line names its file. Empty when nothing does.</summary>
    public string Problems
    {
        get
        {
            var several = this.selection.Count > 1;
            return string.Join(
                Environment.NewLine,
                this.selection.Where(static file => file.Status == ModelImportFileStatus.Error)
                    .Select(file => several ? $"{file.FileName}: {file.StatusText}" : file.StatusText));
        }
    }

    /// <summary>Gets a value indicating whether something blocks the import of a selected file.</summary>
    public bool HasProblems => this.selection.Any(static file => file.Status == ModelImportFileStatus.Error);

    /// <summary>Gets the note on selected models already imported in place, which the import imports again with the
    /// settings shown, read from their last import; empty when none is.</summary>
    public string ReimportNote
    {
        get
        {
            var reimported = this.selection.Where(static file => file.IsReimport).ToArray();
            return reimported.Length switch
            {
                0 => string.Empty,
                1 => $"{reimported[0].FileName} is already imported. Import imports it again with these settings, which start from its last import.",
                _ => string.Create(CultureInfo.CurrentCulture, $"{reimported.Length} of the selected files are already imported. Import imports them again with these settings, which start from their last import."),
            };
        }
    }

    /// <summary>Gets a value indicating whether a selected model is already imported in place.</summary>
    public bool HasReimportNote => this.selection.Any(static file => file.IsReimport);

    /// <summary>Gets a value indicating whether a command failed.</summary>
    public bool HasError => this.ErrorText.Length != 0;

    /// <summary>Gets a value indicating whether a note is shown.</summary>
    public bool HasNotice => this.NoticeText.Length != 0;

    /// <summary>Gets a value indicating whether no file is listed yet, when the dialog shows its empty state.</summary>
    public bool IsEmpty => this.Files.Count == 0;

    /// <summary>Gets a value indicating whether a file is listed, when the dialog shows the list and its settings.</summary>
    public bool HasFiles => !this.IsEmpty;

    /// <summary>Gets a value indicating whether files are listed but none is selected, when the dialog asks to select some.</summary>
    public bool ShowsSelectionHint => this.HasFiles && !this.HasSelection;

    /// <summary>Gets a value indicating whether a file is selected.</summary>
    public bool HasSelection => this.selection.Count != 0;

    /// <summary>Gets a value indicating whether exactly one file is selected; Name shows only then.</summary>
    public bool IsSingleSelection => this.selection.Count == 1;

    /// <summary>Gets or sets the selected file's model name.</summary>
    public string SelectedName
    {
        get => this.IsSingleSelection ? this.selection[0].Name : string.Empty;
        set
        {
            if (this.IsSingleSelection && !string.Equals(this.selection[0].Name, value, StringComparison.Ordinal))
            {
                this.selection[0].Name = value ?? string.Empty;
                this.selection[0].Replacement = null;
                this.CheckFiles();
                this.OnPropertyChanged();
            }
        }
    }

    /// <summary>Gets a value indicating whether the selected files go into different folders.</summary>
    public bool IsFolderMixed => this.selection.Select(static file => file.DestinationFolder).Distinct(StringComparer.Ordinal).Skip(1).Any();

    /// <summary>Gets or sets the folder of every selected file; empty while they differ.</summary>
    public string SelectedFolder
    {
        get => this.IsFolderMixed || !this.HasSelection ? string.Empty : this.selection[0].DestinationFolder;
        set
        {
            if (!this.HasSelection || value is null || (!this.IsFolderMixed && string.Equals(this.SelectedFolder, value, StringComparison.Ordinal)))
            {
                return;
            }

            foreach (var file in this.selection)
            {
                file.DestinationFolder = value;
                file.Replacement = null;
            }

            this.CheckFiles();
            this.OnPropertyChanged();
            this.OnPropertyChanged(nameof(this.IsFolderMixed));
        }
    }

    /// <summary>Gets a value indicating whether a selected file's name is taken and its replacement not yet chosen.</summary>
    public bool HasNamesToReplace => this.selection.Any(static file => file.IsNameTaken && file.Replacement is null);

    /// <summary>Gets the models the selected files replace, once Replace is chosen.</summary>
    public IReadOnlyList<string> Replacements
        => [.. this.selection.Where(static file => file.Replacement is not null).Select(static file => $"{file.FileName} replaces {file.Replacement!.SourceUri.VirtualPath}")];

    /// <summary>Lists the selection the view shows; the rest of the dialog follows it.</summary>
    /// <param name="selected">The selected files.</param>
    public void UpdateSelection(IEnumerable<ModelImportFile> selected)
    {
        ArgumentNullException.ThrowIfNull(selected);
        var chosen = selected.ToHashSet();
        this.selection = [.. this.Files.Where(chosen.Contains)];
        this.Settings.Show(this.selection);
        this.RefreshSelectionText();
        this.NotifySelectionStates();
        this.NotifyListCommands();
        this.OnPropertyChanged(nameof(this.Selection));
        this.OnPropertyChanged(nameof(this.HasSelection));
        this.OnPropertyChanged(nameof(this.ShowsSelectionHint));
        this.OnPropertyChanged(nameof(this.IsSingleSelection));
        this.OnPropertyChanged(nameof(this.SelectedName));
        this.OnPropertyChanged(nameof(this.SelectedFolder));
        this.OnPropertyChanged(nameof(this.IsFolderMixed));
    }

    /// <summary>Checks every file again before the import starts: its source exists and its name is free or replaces
    /// a model.</summary>
    /// <returns>Whether the import can start.</returns>
    public bool Validate()
    {
        this.CheckFiles();
        return this.CanImport;
    }

    /// <summary>Gets what the person chose: each file, in the list's order, and the run's settings.</summary>
    /// <returns>The plan.</returns>
    /// <exception cref="InvalidOperationException">A file cannot be imported.</exception>
    public ModelImportPlan ToPlan()
        => this.CanImport
            ? new([.. this.Files.Select(static file => new ModelImportChoice(file.SourcePath, file.Name.Trim(), ImportDestination.Parse(file.DestinationFolder), file.SettingsWithName())
            {
                Replaces = file.Replacement?.SourceUri,
            })], this.Run.ToSettings())
            : throw new InvalidOperationException("A file of the import cannot be imported.");

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.lifetime.Cancel();
        this.lifetime.Dispose();
        this.analyses.Dispose();
    }

    private static JsonElement NewSettings(string name)
        => JsonSerializer.SerializeToElement(new Dictionary<string, string>(StringComparer.Ordinal) { [ModelImportSettingCatalog.NameKey] = name });

    private static string FactsLine(ModelImportFile file)
        => file.IsMissing ? string.Empty
            : file.IsAnalyzing ? "Reading the file…"
            : file.Facts is { } facts ? FactsText.Line(facts)
            : file.Analysis is not null ? "The file could not be read; see Before import."
            : string.Empty;

    private static ImmutableArray<BeforeImportItem> ReportedBy(ModelImportFile file)
        => BeforeImportItem.From(file.Analysis?.Diagnostics.Where(static issue => issue.Severity >= DiagnosticSeverity.Warning) ?? []);

    // A model already under an authoring mount is imported in place: its identity is its own path.
    private AssetPath? ProjectSourceIdentity(string sourcePath)
    {
        var source = FileSystemPath.From(Path.GetFullPath(sourcePath));
        foreach (var mount in this.project.AuthoringMounts)
        {
            var root = this.project.ProjectRoot.Combine(mount.RelativePath);
            if (source != root && source.IsSameOrInside(root))
            {
                return AssetPath.Create(mount.Name, source.GetPathBelow(root));
            }
        }

        return null;
    }

    // The identity a file's model takes: its own path in the project, else its file in a folder named for it.
    private AssetPath PlannedIdentity(ModelImportFile file)
        => this.ProjectSourceIdentity(file.SourcePath)
            ?? ImportDestination.Parse(file.DestinationFolder).Combine(file.Name.Trim() + "/" + file.FileName);

    // Marks missing sources and names that are taken, by the project or by another file of this import.
    private void CheckFiles()
    {
        var planned = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var records = new ModelImportRecords(this.project.FileSystem, this.project.ProjectRoot.FullPath);
        foreach (var file in this.Files)
        {
            file.IsMissing = !this.project.FileSystem.File.Exists(file.SourcePath);
            file.NameProblem = this.CheckName(file, planned, records);
        }

        this.CanImport = this.Files.Count != 0 && this.Files.All(static file => file.Status != ModelImportFileStatus.Error);
        this.NotifySelectionStates();
    }

    // The selection's problems and notes, which file checks and selection changes both change.
    private void NotifySelectionStates()
    {
        this.OnPropertyChanged(nameof(this.Problems));
        this.OnPropertyChanged(nameof(this.HasProblems));
        this.OnPropertyChanged(nameof(this.ReimportNote));
        this.OnPropertyChanged(nameof(this.HasReimportNote));
        this.OnPropertyChanged(nameof(this.HasNamesToReplace));
        this.OnPropertyChanged(nameof(this.Replacements));
        this.ReplaceCommand.NotifyCanExecuteChanged();
    }

    private string CheckName(ModelImportFile file, HashSet<string> planned, ModelImportRecords records)
    {
        file.IsNameTaken = false;
        try
        {
            var name = file.Name.Trim();
            var folder = ImportDestination.Parse(file.DestinationFolder);
            if (this.ProjectSourceIdentity(file.SourcePath) is { } inPlace)
            {
                // A model already imported in place is imported again: its record takes these settings.
                file.IsReimport = records.IsImported(file.SourcePath);
                return planned.Add(inPlace.VirtualPath) ? string.Empty : $"{file.FileName} is in this import twice.";
            }

            file.IsReimport = false;

            if (!planned.Add(this.PlannedIdentity(file).VirtualPath))
            {
                return $"Another file of this import is named {name} in {folder}.";
            }

            if (!this.project.FileSystem.Directory.Exists(ImportSourceRetention.ResolveDestination(this.project, folder, name)))
            {
                file.Replacement = null;
                return string.Empty;
            }

            file.IsNameTaken = true;
            return file.Replacement is null
                ? $"A model named {name} is already in {folder}. Replace it, or choose another name."
                : string.Empty;
        }
        catch (Exception error) when (error is ArgumentException or InvalidOperationException)
        {
            return error.Message;
        }
    }

    // The dialog marks a run with custom settings; it learns of a change through this model.
    private void OnRunChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs args)
    {
        if (string.Equals(args.PropertyName, nameof(ImportRunViewModel.HasCustomSettings), StringComparison.Ordinal))
        {
            this.OnPropertyChanged(nameof(this.Run));
        }
    }

    private void OnFilesChanged()
    {
        this.CheckFiles();
        this.OnPropertyChanged(nameof(this.IsEmpty));
        this.OnPropertyChanged(nameof(this.HasFiles));
        this.OnPropertyChanged(nameof(this.ShowsSelectionHint));
        this.NotifyListCommands();
    }

    // Selects files for the person, such as those just added; a list not shown yet shows the selection when it loads.
    private void RequestSelection(IReadOnlyList<ModelImportFile> files)
    {
        this.UpdateSelection(files);
        this.SelectionRequested?.Invoke(this, files);
    }

    private void RefreshSelectionText()
    {
        this.Facts = this.selection.Count switch
        {
            0 => string.Empty,
            1 => FactsLine(this.selection[0]),
            _ => string.Create(CultureInfo.CurrentCulture, $"{this.selection.Count} files selected"),
        };
        var several = this.selection.Count > 1;
        this.BeforeImport =
        [
            .. this.selection
                .Select(file => new BeforeImportGroup(file.FileName, ReportedBy(file), several))
                .Where(static group => !group.Items.IsEmpty),
        ];
    }
}
