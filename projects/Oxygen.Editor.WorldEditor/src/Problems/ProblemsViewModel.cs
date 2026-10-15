// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.ContentBrowser.Scripts;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Play;

namespace Oxygen.Editor.World.Problems;

/// <summary>
/// The Problems panel: script problems of the project and of the running scene, grouped by file, with the full
/// diagnostic of the selected one.
/// </summary>
/// <remarks>
/// Problems update as scripts are saved, without taking focus; the tab title carries the error and warning counts.
/// An analysis entry clears when its file is fixed or deleted; a runtime entry clears when the next session starts.
/// </remarks>
public sealed partial class ProblemsViewModel : ObservableObject, IDisposable
{
    /// <summary>The panel name without counts.</summary>
    public const string BaseTitle = "Problems";

    /// <summary>The group name of runtime problems that have no source file.</summary>
    public const string PlaySessionGroup = "Play session";

    private readonly IScriptAnalysisService analysis;
    private readonly ICodeEditorService codeEditor;
    private readonly IProjectContextService projects;
    private readonly Dictionary<string, ScriptAnalysisSnapshot> files = new(StringComparer.OrdinalIgnoreCase);
    private readonly IDisposable subscription;
    private readonly IRuntimeProblemSource? runtime;

    /// <summary>Initializes a new instance of the <see cref="ProblemsViewModel"/> class.</summary>
    /// <param name="analysis">The script analysis, whose changes are raised on the UI thread.</param>
    /// <param name="codeEditor">The code editor that opens a problem's location.</param>
    /// <param name="projects">The active project, for project-relative file names.</param>
    /// <param name="runtime">The problems the running scene reports.</param>
    public ProblemsViewModel(IScriptAnalysisService analysis, ICodeEditorService codeEditor, IProjectContextService projects, IRuntimeProblemSource? runtime = null)
    {
        ArgumentNullException.ThrowIfNull(analysis);
        this.analysis = analysis;
        this.codeEditor = codeEditor;
        this.projects = projects;
        foreach (var snapshot in analysis.Current)
        {
            this.files[snapshot.SourcePath] = snapshot;
        }

        this.runtime = runtime;
        if (runtime is not null)
        {
            runtime.RuntimeProblemsChanged += this.OnRuntimeProblemsChanged;
        }

        this.Rebuild();
        this.subscription = analysis.Changes.Subscribe(this.OnAnalyzed);
    }

    /// <summary>Gets the files with problems, sorted by path.</summary>
    public ObservableCollection<ProblemFileGroup> Groups { get; } = [];

    /// <summary>Gets or sets the selected problem.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasSelection), nameof(CanSelectNode))]
    [NotifyCanExecuteChangedFor(nameof(OpenSelectedCommand), nameof(SelectNodeCommand))]
    public partial ProblemEntry? Selected { get; set; }

    /// <summary>Gets the number of errors.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(Title), nameof(IsEmpty))]
    public partial int ErrorCount { get; private set; }

    /// <summary>Gets the number of warnings.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(Title), nameof(IsEmpty))]
    public partial int WarningCount { get; private set; }

    /// <summary>Gets a value indicating whether every script is being checked again.</summary>
    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(CheckScriptsCommand))]
    public partial bool IsChecking { get; private set; }

    /// <summary>Gets the tab title: <c>Problems</c> with its error and warning counts.</summary>
    public string Title => FormatTitle(this.ErrorCount, this.WarningCount);

    /// <summary>Gets a value indicating whether there is nothing to show.</summary>
    public bool IsEmpty => this.Groups.Count == 0;

    /// <summary>Gets a value indicating whether a problem is selected.</summary>
    public bool HasSelection => this.Selected is not null;

    /// <summary>Gets a value indicating whether the selected problem names a node of the scene that played.</summary>
    public bool CanSelectNode => this.Selected?.Problem is { IsRuntime: true, SlotNode: not null } && this.runtime is not null;

    /// <summary>Formats the tab title.</summary>
    /// <param name="errors">The number of errors.</param>
    /// <param name="warnings">The number of warnings.</param>
    /// <returns><c>Problems</c>, followed by the non-zero counts.</returns>
    public static string FormatTitle(int errors, int warnings)
    {
        var title = BaseTitle;
        if (errors > 0)
        {
            title += $"  ⛔ {errors}";
        }

        if (warnings > 0)
        {
            title += $"  ⚠ {warnings}";
        }

        return title;
    }

    /// <inheritdoc />
    public void Dispose()
    {
        this.subscription.Dispose();
        if (this.runtime is not null)
        {
            this.runtime.RuntimeProblemsChanged -= this.OnRuntimeProblemsChanged;
        }
    }

    /// <summary>Opens a problem's location in the code editor.</summary>
    /// <param name="entry">The problem.</param>
    /// <returns>A task that completes when the editor was asked to open the file.</returns>
    [RelayCommand]
    public async Task OpenAsync(ProblemEntry? entry)
    {
        if (entry is not null && HasSourceFile(entry))
        {
            _ = await this.codeEditor.OpenAsync(entry.Problem.SourcePath, entry.Problem.Line, entry.Problem.Column).ConfigureAwait(true);
        }
    }

    /// <summary>Selects a problem, when Problems lists it.</summary>
    /// <param name="problem">The problem.</param>
    /// <returns>Whether the problem was found and selected.</returns>
    public bool Select(ScriptProblem problem)
    {
        ArgumentNullException.ThrowIfNull(problem);
        this.Selected = this.Groups.SelectMany(static group => group.Entries).FirstOrDefault(entry => entry.Problem == problem) ?? this.Selected;
        return this.Selected?.Problem == problem;
    }

    // A runtime problem whose spans name no project file has nothing to open.
    private static bool HasSourceFile(ProblemEntry entry) => Path.IsPathFullyQualified(entry.Problem.SourcePath);

    private bool CanCheckScripts() => !this.IsChecking;

    [RelayCommand(CanExecute = nameof(CanCheckScripts))]
    private async Task CheckScriptsAsync()
    {
        this.IsChecking = true;
        try
        {
            await this.analysis.RefreshAsync().ConfigureAwait(true);
        }
        finally
        {
            this.IsChecking = false;
        }
    }

    private bool CanOpenSelected() => this.Selected is { } entry && HasSourceFile(entry);

    [RelayCommand(CanExecute = nameof(CanSelectNode))]
    private async Task SelectNodeAsync()
    {
        if (this.Selected is { } entry && this.runtime is { } source)
        {
            _ = await source.SelectProblemNodeAsync(entry.Problem).ConfigureAwait(true);
        }
    }

    [RelayCommand(CanExecute = nameof(CanOpenSelected))]
    private Task OpenSelectedAsync() => this.OpenAsync(this.Selected);

    private void OnAnalyzed(ScriptAnalysisSnapshot snapshot)
    {
        if (snapshot.IsRemoved || snapshot.Problems.Count == 0)
        {
            _ = this.files.Remove(snapshot.SourcePath);
        }
        else
        {
            this.files[snapshot.SourcePath] = snapshot;
        }

        this.Rebuild();
    }

    private void OnRuntimeProblemsChanged(object? sender, EventArgs args) => this.Rebuild();

    private void Rebuild()
    {
        var selected = this.Selected?.Problem;
        var root = this.projects.ActiveProject?.ProjectRoot;
        var runtime = this.runtime?.RuntimeProblems ?? [];
        var groups = this.files.Values
            .Where(static file => file.Problems.Count > 0)
            .SelectMany(static file => file.Problems)
            .Concat(runtime)
            .GroupBy(static problem => problem.SourcePath, StringComparer.OrdinalIgnoreCase)
            .Select(file => new ProblemFileGroup(
                file.Key,
                file.Key.Length == 0 ? PlaySessionGroup
                    : root is null || !Path.IsPathFullyQualified(file.Key) ? file.Key
                    : Path.GetRelativePath(root, file.Key).Replace('\\', '/'),
                [.. file.OrderBy(static problem => problem.Line ?? 0).ThenBy(static problem => problem.Column ?? 0).Select(static problem => new ProblemEntry(problem))]))
            .OrderBy(static group => group.DisplayPath, StringComparer.OrdinalIgnoreCase)
            .ToList();

        this.Groups.Clear();
        foreach (var group in groups)
        {
            this.Groups.Add(group);
        }

        this.Selected = selected is null ? null : groups.SelectMany(static group => group.Entries).FirstOrDefault(entry => entry.Problem == selected);
        this.ErrorCount = groups.Sum(static group => group.Entries.Count(static entry => entry.IsError));
        this.WarningCount = groups.Sum(static group => group.Entries.Count(static entry => entry.IsWarning));
        this.OnPropertyChanged(nameof(this.IsEmpty));
    }
}
