// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.ViewModels;

using System.Collections.ObjectModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Collections;
using DroidNet.Hosting.Generators;
using DroidNet.Routing;
using Microsoft.Extensions.DependencyInjection;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Templates;

/// <summary>ViewModel for the initial page displaying when starting the project browser.</summary>
[InjectAs(ServiceLifetime.Singleton)]
public partial class HomeViewModel : IRoutingAware
{
    private readonly IRouter router;
    private readonly IProjectsService projectsService;
    private readonly ITemplatesService templateService;

    public HomeViewModel(
        IRouter router,
        ITemplatesService templateService,
        IProjectsService projectsService)
    {
        this.router = router;
        this.templateService = templateService;
        this.projectsService = projectsService;
    }

    public IActiveRoute? ActiveRoute { get; set; }

    public ObservableCollection<ITemplateInfo> Templates { get; } = new();

    public ObservableCollection<IProjectInfo> RecentProjects { get; } = new();

    [RelayCommand]
    public void LoadTemplates()
    {
        Debug.WriteLine("StartHomeViewModel Clear Templates");
        this.Templates.Clear();
        Debug.WriteLine("StartHomeViewModel Load Templates");

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
    public void LoadRecentProjects()
    {
        Debug.WriteLine("StartHomeViewModel Clear Recent Projects");
        this.RecentProjects.Clear();
        Debug.WriteLine("StartHomeViewModel Load Recent Projects");

        _ = this.projectsService.GetRecentlyUsedProjects()
            .Subscribe(
                projectInfo => this.RecentProjects.InsertInPlace(
                    projectInfo,
                    x => x.LastUsedOn,
                    new DateTimeComparerDescending()));
    }

    [RelayCommand]
    public void MoreTemplates()
    {
        if (this.ActiveRoute is not null)
        {
            this.router.Navigate("new", new PartialNavigation() { RelativeTo = this.ActiveRoute.Parent });
        }
    }

    [RelayCommand]
    public void MoreProjects()
    {
        if (this.ActiveRoute is not null)
        {
            this.router.Navigate("open", new PartialNavigation() { RelativeTo = this.ActiveRoute.Parent });
        }
    }

    public async Task<bool> NewProjectFromTemplate(ITemplateInfo template, string projectName, string location)
    {
        Debug.WriteLine(
            $"New project from template: {template.Category!.Name}/{template.Name} with name `{projectName}` in location `{location}`");

        return await this.projectsService.NewProjectFromTemplate(template, projectName, location).ConfigureAwait(true);
    }

    public void OpenProject(IProjectInfo projectInfo)
        => this.projectsService.LoadProjectAsync(projectInfo.Location!).Wait();

    private sealed class DateTimeComparerDescending : Comparer<DateTime>
    {
        public override int Compare(DateTime x, DateTime y) => y.CompareTo(x);
    }
}
