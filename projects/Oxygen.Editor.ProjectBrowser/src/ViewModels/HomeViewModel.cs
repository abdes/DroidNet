// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Collections;
using DroidNet.Routing;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ProjectBrowser.ViewModels;

/// <summary>
/// ViewModel for the Home view in the Oxygen Editor's Project Browser.
/// </summary>
/// <param name="router">The router for navigating between views.</param>
/// <param name="templateService">The service for managing project templates.</param>
/// <param name="projectBrowser">The service for managing projects.</param>
public partial class HomeViewModel(
    IRouter router,
    ITemplatesService templateService,
    IProjectBrowserService projectBrowser) : ObservableObject, IRoutingAware
{
    private readonly DispatcherQueue dispatcherQueue = DispatcherQueue.GetForCurrentThread();

    [ObservableProperty]
    private ITemplateInfo? selectedTemplate;

    /// <inheritdoc/>
    public IActiveRoute? ActiveRoute { get; set; }

    /// <summary>
    /// Gets the collection of project templates.
    /// </summary>
    public ObservableCollection<ITemplateInfo> Templates { get; } = [];

    /// <summary>
    /// Gets the collection of recent projects.
    /// </summary>
    public ObservableCollection<IProjectInfo> RecentProjects { get; } = [];

    /// <summary>
    /// Creates a new project from the specified template.
    /// </summary>
    /// <param name="template">The template to use for the new project.</param>
    /// <param name="projectName">The name of the new project.</param>
    /// <param name="location">The location where the project will be created.</param>
    /// <returns>A task that represents the asynchronous operation. The task result is <see langword="true"/> if the project was created successfully; otherwise, <see langword="false"/>.</returns>
    public async Task<bool> NewProjectFromTemplate(ITemplateInfo template, string projectName, string location)
        => await projectBrowser.NewProjectFromTemplate(template, projectName, location).ConfigureAwait(true);

    /// <summary>
    /// Opens an existing project.
    /// </summary>
    /// <param name="projectInfo">The information of the project to open.</param>
    /// <returns>A task that represents the asynchronous operation. The task result is <see langword="true"/> if the project was opened successfully; otherwise, <see langword="false"/>.</returns>
    public async Task<bool> OpenProjectAsync(IProjectInfo projectInfo)
        => await projectBrowser.OpenProjectAsync(projectInfo).ConfigureAwait(true)
           && this.dispatcherQueue.TryEnqueue(() => router.Navigate("/we", new FullNavigation()));

    /// <summary>
    /// Navigates to the view for more templates.
    /// </summary>
    [RelayCommand]
    private void MoreTemplates()
    {
        if (this.ActiveRoute is not null)
        {
            router.Navigate("new", new PartialNavigation() { RelativeTo = this.ActiveRoute.Parent });
        }
    }

    /// <summary>
    /// Navigates to the view for more projects.
    /// </summary>
    [RelayCommand]
    private void MoreProjects()
    {
        if (this.ActiveRoute is not null)
        {
            router.Navigate("open", new PartialNavigation() { RelativeTo = this.ActiveRoute.Parent });
        }
    }

    /// <summary>
    /// Loads the project templates asynchronously.
    /// </summary>
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
                    x => x.LastUsedOn!,
                    new DateTimeComparerDescending());
            }

            this.SelectedTemplate = this.Templates.FirstOrDefault();
        }
    }

    /// <summary>
    /// Loads the recent projects asynchronously.
    /// </summary>
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

    /// <summary>
    /// Compares <see cref="DateTime"/> values in descending order.
    /// </summary>
    private sealed class DateTimeComparerDescending : Comparer<DateTime>
    {
        /// <inheritdoc/>
        public override int Compare(DateTime x, DateTime y) => y.CompareTo(x);
    }
}
