// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.ViewModels;

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.DependencyInjection;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Services;
using Oxygen.Editor.ProjectBrowser.Templates;

public partial class NewProjectDialogViewModel : ObservableObject
{
    [ObservableProperty]
    private List<QuickSaveLocation> pinnedLocations;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsProjectNameValid))]
    private string projectName;

    [ObservableProperty]
    private QuickSaveLocation selectedLocation;

    [ObservableProperty]
    private ITemplateInfo template;

    public NewProjectDialogViewModel(ITemplateInfo template)
    {
        this.Template = template;

        var projectsService = Ioc.Default.GetRequiredService<IProjectsService>();
        this.PinnedLocations = projectsService.GetQuickSaveLocations();
        this.SelectedLocation = this.PinnedLocations.First();

        this.ProjectName = string.Empty;
    }

    public bool IsProjectNameValid
        => this.ProjectName != string.Empty; // TODO(abdes) validate project name, use CanCreateProject

    [RelayCommand]
    private void SetLocation(QuickSaveLocation location)
        => this.SelectedLocation = location;
}
