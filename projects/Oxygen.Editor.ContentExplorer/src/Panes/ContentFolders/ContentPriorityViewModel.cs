// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>Edits source precedence with the highest-priority source displayed first.</summary>
public sealed partial class ContentPriorityViewModel : ObservableObject
{
    /// <summary>Initializes a new instance of the <see cref="ContentPriorityViewModel"/> class.</summary>
    /// <param name="project">The accepted project configuration.</param>
    public ContentPriorityViewModel(ProjectContext project)
    {
        foreach (var source in CookedContentOrdering.Resolve(project).Reverse())
        {
            switch (source.Kind)
            {
                case CookedContentSourceKind.LocalFolder:
                    var folder = project.LocalFolderMounts.Single(mount => string.Equals(mount.Name, source.Name, StringComparison.Ordinal));

                    // A folder without an index holds no cooked content to order yet.
                    if (project.FileSystem.File.Exists(Path.Combine(folder.Root.FullPath, "container.index.bin")))
                    {
                        this.Sources.Add(new(source, folder.Name, folder.Root.FullPath));
                    }

                    break;
                case CookedContentSourceKind.PakFile:
                    var pak = project.PakFileMounts.Single(mount => string.Equals(mount.Name, source.Name, StringComparison.Ordinal));
                    this.Sources.Add(new(source, pak.Name, pak.Path.FullPath));
                    break;
                case CookedContentSourceKind.ProjectOutput:
                    this.Sources.Add(new(source, "Project output", Path.Combine(project.ProjectRoot.FullPath, ".cooked")));
                    break;
                default:
                    throw new InvalidDataException("Content priority names an unknown source kind.");
            }
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
