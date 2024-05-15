// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.ViewModels;

using System.Collections.ObjectModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Collections;
using DroidNet.Hosting.Generators;
using DroidNet.Routing;
using Microsoft.Extensions.DependencyInjection;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Templates;

/// <summary>ViewModel for the initial page displaying when starting the project browser.</summary>
[InjectAs(ServiceLifetime.Singleton)]
public partial class HomeViewModel(
    IRouter router,
    ITemplatesService templateService,
    IProjectsService projectsService) : ObservableObject, IRoutingAware
{
    [ObservableProperty]
    private ITemplateInfo? selectedTemplate;

    public IActiveRoute? ActiveRoute { get; set; }

    public ObservableCollection<ITemplateInfo> Templates { get; } = [];

    public ObservableCollection<IProjectInfo> RecentProjects { get; } = [];

    [RelayCommand]
    public async Task LoadTemplates()
    {
        Debug.WriteLine("StartHomeViewModel Clear Templates");
        this.Templates.Clear();
        Debug.WriteLine("StartHomeViewModel Load Templates");

        if (templateService.HasRecentlyUsedTemplates())
        {
            _ = templateService.GetRecentlyUsedTemplates()
                .Subscribe(template => this.Templates.InsertInPlace(template, x => x.LastUsedOn));
        }
        else
        {
            await foreach (var template in templateService.GetLocalTemplatesAsync().ConfigureAwait(true))
            {
                this.Templates.InsertInPlace(
                    template,
                    x => (DateTime)x.LastUsedOn!,
                    new DateTimeComparerDescending());
            }

            this.SelectedTemplate = this.Templates[0];
        }
    }

    [RelayCommand]
    public async Task LoadRecentProjectsAsync()
    {
        Debug.WriteLine("StartHomeViewModel Clear Recent Projects");
        this.RecentProjects.Clear();
        Debug.WriteLine("StartHomeViewModel Load Recent Projects");

        await foreach (var projectInfo in projectsService.GetRecentlyUsedProjectsAsync().ConfigureAwait(true))
        {
            this.RecentProjects.InsertInPlace(
                projectInfo,
                x => x.LastUsedOn,
                new DateTimeComparerDescending());
        }
    }

    [RelayCommand]
    public void MoreTemplates()
    {
        if (this.ActiveRoute is not null)
        {
            router.Navigate("new", new PartialNavigation() { RelativeTo = this.ActiveRoute.Parent });
        }
    }

    [RelayCommand]
    public void MoreProjects()
    {
        if (this.ActiveRoute is not null)
        {
            router.Navigate("open", new PartialNavigation() { RelativeTo = this.ActiveRoute.Parent });
        }
    }

    public async Task<bool> NewProjectFromTemplate(ITemplateInfo template, string projectName, string location)
    {
        Debug.WriteLine(
            $"New project from template: {template.Category.Name}/{template.Name} with name `{projectName}` in location `{location}`");

        return await projectsService.NewProjectFromTemplate(template, projectName, location).ConfigureAwait(true);
    }

    public void OpenProject(IProjectInfo projectInfo)
        => projectsService.LoadProjectAsync(projectInfo.Location!).Wait();

    private sealed class DateTimeComparerDescending : Comparer<DateTime>
    {
        public override int Compare(DateTime x, DateTime y) => y.CompareTo(x);
    }
}
