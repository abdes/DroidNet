// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>Edits source precedence with the highest-priority source displayed first.</summary>
public sealed partial class ContentPriorityViewModel : ObservableObject
{
    /// <summary>Initializes a new instance of the <see cref="ContentPriorityViewModel"/> class.</summary>
    /// <param name="project">The accepted project configuration.</param>
    public ContentPriorityViewModel(ProjectContext project)
    {
        foreach (var source in CookedContentOrdering.Resolve(project.LocalFolderMounts, project.CookedContentOrder).Reverse())
        {
            var library = source.Kind == CookedContentSourceKind.LocalFolder ? project.LocalFolderMounts.Single(mount => string.Equals(mount.Name, source.Name, StringComparison.OrdinalIgnoreCase)) : null;
            if (library is not null && !File.Exists(Path.Combine(library.AbsolutePath, "container.index.bin")))
            {
                continue;
            }

            this.Sources.Add(new(source, library?.Name ?? "Project output", library?.AbsolutePath ?? Path.Combine(project.ProjectRoot, ".cooked")));
        }

        this.SelectedSource = this.Sources.FirstOrDefault();
    }

    /// <summary>Gets sources from highest to lowest priority.</summary>
    public ObservableCollection<ContentPriorityItem> Sources { get; } = [];

    /// <summary>Gets or sets the source being moved.</summary>
    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(IncreasePriorityCommand))]
    [NotifyCanExecuteChangedFor(nameof(DecreasePriorityCommand))]
    public partial ContentPriorityItem? SelectedSource { get; set; }

    /// <summary>Returns native mount order, with the highest-priority source last.</summary>
    /// <returns>The order to persist on explicit Apply.</returns>
    public IReadOnlyList<CookedContentSource> GetMountOrder() => this.Sources.Reverse().Select(static item => item.Source).ToArray();

    private bool CanIncreasePriority() => this.SelectedSource is { } source && this.Sources.IndexOf(source) > 0;

    private bool CanDecreasePriority() => this.SelectedSource is { } source && this.Sources.IndexOf(source) is var index && index >= 0 && index < this.Sources.Count - 1;

    [RelayCommand(CanExecute = nameof(CanIncreasePriority))]
    private void IncreasePriority() => this.Move(-1);

    [RelayCommand(CanExecute = nameof(CanDecreasePriority))]
    private void DecreasePriority() => this.Move(1);

    private void Move(int offset)
    {
        if (this.SelectedSource is not { } source)
        {
            return;
        }

        var index = this.Sources.IndexOf(source);
        if (index < 0 || index + offset < 0 || index + offset >= this.Sources.Count)
        {
            return;
        }

        this.Sources.Move(index, index + offset);
        this.SelectedSource = source;
        this.IncreasePriorityCommand.NotifyCanExecuteChanged();
        this.DecreasePriorityCommand.NotifyCanExecuteChanged();
    }
}
