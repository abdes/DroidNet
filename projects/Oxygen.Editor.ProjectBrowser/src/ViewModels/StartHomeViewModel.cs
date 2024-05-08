// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.ViewModels;

using System.Collections.ObjectModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Collections;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Services;
using Oxygen.Editor.ProjectBrowser.Templates;

public partial class StartHomeViewModel
{
    private readonly IProjectsService projectsService;
    private readonly ITemplatesService templateService;

    public StartHomeViewModel(
        ITemplatesService templateService,
        IProjectsService projectsService,
        StartViewModel navigationViewModel)
    {
        this.templateService = templateService;
        this.projectsService = projectsService;
        this.NavigationViewModel = navigationViewModel;
    }

    public StartViewModel NavigationViewModel { get; init; }

    public ObservableCollection<ITemplateInfo> Templates { get; } = new();

    public ObservableCollection<IProjectInfo> RecentProjects { get; } = new();

    [RelayCommand]
    public void LoadTemplates()
    {
        Debug.WriteLine($"StartHomeViewModel Clear Templates");
        this.Templates.Clear();
        Debug.WriteLine($"StartHomeViewModel Load Templates");

        if (this.templateService.HasRecentlyUsedTemplates())
        {
            _ = this.templateService.GetRecentlyUsedTemplates()
                .Subscribe(template => this.Templates.InsertInPlace(template, x => x.LastUsedOn));
        }
        else
        {
            _ = this.templateService.GetLocalTemplates()
                .Subscribe(
                    template => this.Templates.InsertInPlace(
                        template,
                        x => (DateTime)x.LastUsedOn!,
                        new DateTimeComparerDescending()));
        }
    }

    [RelayCommand]
    public async Task LoadRecentProjects()
    {
        Debug.WriteLine($"StartHomeViewModel Clear Recent Projects");
        this.RecentProjects.Clear();
        Debug.WriteLine($"StartHomeViewModel Load Recent Projects");

        await foreach (var project in this.projectsService.GetRecentlyUsedProjectsAsync())
        {
            this.RecentProjects.InsertInPlace(project, x => x.LastUsedOn, new DateTimeComparerDescending());
        }
    }

    public async Task<bool> NewProjectFromTemplate(ITemplateInfo template, string projectName, string location)
    {
        Debug.WriteLine(
            $"New project from template: {template.Category!.Name}/{template.Name} with name `{projectName}` in location `{location}`");

        return await this.projectsService.NewProjectFromTemplate(template, projectName, location);
    }

    public void OpenProject(IProjectInfo projectInfo) => this.projectsService.LoadProjectAsync(projectInfo.Location!);

    private class DateTimeComparerDescending : Comparer<DateTime>
    {
        public override int Compare(DateTime x, DateTime y) => y.CompareTo(x);
    }
}
