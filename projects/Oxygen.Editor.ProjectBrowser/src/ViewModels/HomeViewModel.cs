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
using Microsoft.UI.Dispatching;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.Projects;

/// <summary>
/// A ViewModel for the Home page of the Project Browser.
/// </summary>
/// <param name="router">
/// The <see cref="IRouter" /> configured for the application. Injected.
/// </param>
/// <param name="templateService">
/// The <see cref="TemplatesService" /> configured for the application. Injected.
/// </param>
/// <param name="projectBrowser">
/// The <see cref="IProjectBrowserService" /> configured for the application. Injected.
/// </param>
/// <param name="projectManager">
/// The <see cref="IProjectManagerService" /> configured for the application. Injected.
/// </param>
[InjectAs(ServiceLifetime.Singleton)]
public partial class HomeViewModel(
    IRouter router,
    ITemplatesService templateService,
    IProjectBrowserService projectBrowser,
    IProjectManagerService projectManager) : ObservableObject, IRoutingAware
{
    private readonly DispatcherQueue dispatcherQueue = DispatcherQueue.GetForCurrentThread();

    [ObservableProperty]
    private ITemplateInfo? selectedTemplate;

    public IActiveRoute? ActiveRoute { get; set; }

    public ObservableCollection<ITemplateInfo> Templates { get; } = [];

    public ObservableCollection<IProjectInfo> RecentProjects { get; } = [];

    public async Task<bool> NewProjectFromTemplate(ITemplateInfo template, string projectName, string location)
    {
        Debug.WriteLine(
            $"New project from template: {template.Category.Name}/{template.Name} with name `{projectName}` in location `{location}`");

        return await projectBrowser.NewProjectFromTemplate(template, projectName, location)
            .ConfigureAwait(true);
    }

    public async Task<bool> OpenProjectAsync(IProjectInfo projectInfo)
    {
        var success = await projectManager.LoadProjectAsync(projectInfo).ConfigureAwait(false);
        if (success)
        {
            success = this.dispatcherQueue.TryEnqueue(() => router.Navigate("/we", new FullNavigation()));
        }

        return success;
    }

    [RelayCommand]
    private void MoreTemplates()
    {
        if (this.ActiveRoute is not null)
        {
            router.Navigate("new", new PartialNavigation() { RelativeTo = this.ActiveRoute.Parent });
        }
    }

    [RelayCommand]
    private void MoreProjects()
    {
        if (this.ActiveRoute is not null)
        {
            router.Navigate("open", new PartialNavigation() { RelativeTo = this.ActiveRoute.Parent });
        }
    }

    [RelayCommand]
    private async Task LoadTemplates()
    {
        this.Templates.Clear();

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
    private async Task LoadRecentProjects()
    {
        // Track the recent projects loaded from the project browser service in
        // a dictionary for fast lookup.
        var recentlyUsedProjectDict = new Dictionary<IProjectInfo, IProjectInfo>();

        // Update existing items and add new items in the RecentProjects collection
        await foreach (var projectInfo in projectBrowser.GetRecentlyUsedProjectsAsync()
                           .ConfigureAwait(true))
        {
            recentlyUsedProjectDict.Add(projectInfo, projectInfo);

            var existingItem = this.RecentProjects.FirstOrDefault(item => item.Equals(projectInfo));
            if (existingItem != null)
            {
                existingItem.LastUsedOn = projectInfo.LastUsedOn;
            }
            else
            {
                this.RecentProjects.Add(projectInfo); // Add new item
            }
        }

        // Remove items not in the collection obtained from the project browser
        for (var index = this.RecentProjects.Count - 1; index >= 0; index--)
        {
            if (!recentlyUsedProjectDict.ContainsKey(this.RecentProjects[index]))
            {
                this.RecentProjects.RemoveAt(index);
            }
        }
    }

    private sealed class DateTimeComparerDescending : Comparer<DateTime>
    {
        public override int Compare(DateTime x, DateTime y) => y.CompareTo(x);
    }
}
