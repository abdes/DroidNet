// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.LivePreview.Play;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Problems;

/// <summary>
/// The Problems panel: the problems of every asset family of the project (script analysis, input sources) and of the
/// running scene, grouped by file, with the full diagnostic of the selected one.
/// </summary>
/// <remarks>
/// Problems update as files are saved, without taking focus; the tab title carries the error and warning counts. A
/// family's problem clears when its file is fixed or deleted; a runtime problem clears when the next session starts.
/// Each source raises its changes on the UI thread, where the panel subscribes.
/// </remarks>
public sealed partial class ProblemsViewModel : ObservableObject, IDisposable
{
    /// <summary>The panel name without counts.</summary>
    public const string BaseTitle = "Problems";

    /// <summary>The group name of problems that have no file, such as the running scene's.</summary>
    public const string PlaySessionGroup = "Play session";

    private readonly IReadOnlyList<IAssetProblemSource> sources;
    private readonly ICodeEditorService codeEditor;
    private readonly IProjectContextService projects;
    private readonly IScripts? scripts;
    private readonly IRuntimeProblemSource? runtime;
    private readonly IRuntimeProblemNodes nodes;

    /// <summary>Initializes a new instance of the <see cref="ProblemsViewModel"/> class.</summary>
    /// <param name="sources">The problem sources: the family facts, and the running scene's problems.</param>
    /// <param name="codeEditor">The code editor that opens a problem's location.</param>
    /// <param name="projects">The active project, for project-relative file names.</param>
    /// <param name="nodes">What shows the node a runtime problem names.</param>
    public ProblemsViewModel(IEnumerable<IAssetProblemSource> sources, ICodeEditorService codeEditor, IProjectContextService projects, IRuntimeProblemNodes nodes)
    {
        ArgumentNullException.ThrowIfNull(sources);
        ArgumentNullException.ThrowIfNull(nodes);
        this.nodes = nodes;
        this.sources = [.. sources.Distinct()];
        this.codeEditor = codeEditor;
        this.projects = projects;
        this.scripts = this.sources.OfType<IScripts>().FirstOrDefault();
        this.runtime = this.sources.OfType<IRuntimeProblemSource>().FirstOrDefault();
        foreach (var source in this.sources)
        {
            source.ProblemsChanged += this.OnProblemsChanged;
        }

        this.Rebuild();
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
    public bool CanSelectNode => this.Selected?.Problem is RuntimeScriptProblem { SlotNode: not null } problem
        && this.runtime?.NodeOf(problem) is not null;

    /// <summary>Formats the tab title.</summary>
    /// <param name="errors">The number of errors.</param>
    /// <param name="warnings">The number of warnings.</param>
    /// <returns><c>Problems</c>, followed by the non-zero counts.</returns>
    public static string FormatTitle(int errors, int warnings)
    {
        var title = BaseTitle;
        if (errors > 0)
        {
            title += string.Create(CultureInfo.InvariantCulture, $"  ⛔ {errors}");
        }

        if (warnings > 0)
        {
            title += string.Create(CultureInfo.InvariantCulture, $"  ⚠ {warnings}");
        }

        return title;
    }

    /// <inheritdoc />
    public void Dispose()
    {
        foreach (var source in this.sources)
        {
            source.ProblemsChanged -= this.OnProblemsChanged;
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
            _ = await this.codeEditor.OpenAsync(entry.Problem.File!, entry.Problem.Line, entry.Problem.Column).ConfigureAwait(true);
        }
    }

    /// <summary>Selects a problem, when Problems lists it.</summary>
    /// <param name="problem">The problem.</param>
    /// <returns>Whether the problem was found and selected.</returns>
    public bool Select(AssetProblem problem)
    {
        ArgumentNullException.ThrowIfNull(problem);
        this.Selected = this.Groups.SelectMany(static group => group.Entries).FirstOrDefault(entry => entry.Problem == problem) ?? this.Selected;
        return this.Selected?.Problem == problem;
    }

    // A runtime problem whose spans name no project file has nothing to open.
    private static bool HasSourceFile(ProblemEntry entry) => entry.Problem.File is { } file && Path.IsPathFullyQualified(file);

    private bool CanCheckScripts() => !this.IsChecking && this.scripts is not null;

    [RelayCommand(CanExecute = nameof(CanCheckScripts))]
    private async Task CheckScriptsAsync()
    {
        this.IsChecking = true;
        try
        {
            await this.scripts!.RefreshAsync(force: true).ConfigureAwait(true);
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
        if (this.Selected?.Problem is RuntimeScriptProblem problem)
        {
            _ = await this.nodes.SelectAsync(problem).ConfigureAwait(true);
        }
    }

    [RelayCommand(CanExecute = nameof(CanOpenSelected))]
    private Task OpenSelectedAsync() => this.OpenAsync(this.Selected);

    private void OnProblemsChanged(object? sender, EventArgs args) => this.Rebuild();

    private void Rebuild()
    {
        var selected = this.Selected?.Problem;
        var root = this.projects.ActiveProject?.ProjectRoot;
        var groups = this.sources
            .SelectMany(static source => source.Problems)
            .GroupBy(static problem => problem.File ?? string.Empty, StringComparer.OrdinalIgnoreCase)
            .Select(file =>
            {
                var displayPath = file.Key.Length == 0 ? PlaySessionGroup
                    : root is not null && Path.IsPathFullyQualified(file.Key) && FileSystemPath.From(file.Key) is var path && path.IsSameOrInside(root) ? path.GetPathBelow(root)
                    : file.Key;
                var entries = file.OrderBy(static problem => problem.Line ?? 0).ThenBy(static problem => problem.Column ?? 0).Select(static problem => new ProblemEntry(problem));
                return new ProblemFileGroup(file.Key, displayPath, [.. entries]);
            })
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
