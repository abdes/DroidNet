// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Collections;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
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
/// <param name="loggerFactory">
/// The <see cref="ILoggerFactory" /> used to obtain an <see cref="ILogger" />. If the logger
/// cannot be obtained, a <see cref="NullLogger" /> is used silently.
/// </param>
public partial class HomeViewModel(
    HostingContext hostingContext,
    IRouter router,
    ITemplatesService templateService,
    IProjectBrowserService projectBrowser,
    ILoggerFactory? loggerFactory = null) : ObservableObject, IRoutingAware
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<NewProjectViewModel>() ?? NullLoggerFactory.Instance.CreateLogger<NewProjectViewModel>();

    private IActiveRoute? activeRoute;
    private bool preloadedTemplates;
    private bool preloadedProjects;

    [ObservableProperty]
    private ITemplateInfo? selectedTemplate;

    /// <summary>
    /// Gets the collection of project templates.
    /// </summary>
    public ObservableCollection<ITemplateInfo> RecentTemplates { get; } = [];

    /// <summary>
    /// Gets the collection of recent projects.
    /// </summary>
    public ObservableCollection<IProjectInfo> RecentProjects { get; } = [];

    /// <inheritdoc/>
    public async Task OnNavigatedToAsync(IActiveRoute route)
    {
        this.activeRoute = route;
        await this.PreloadRecentTemplatesAsync().ConfigureAwait(true);
        await this.PreloadRecentProjectsAsync().ConfigureAwait(true);
    }

    /// <summary>
    /// Creates a new project from the specified template.
    /// </summary>
    /// <param name="template">The template to use for the new project.</param>
    /// <param name="projectName">The name of the new project.</param>
    /// <param name="location">The location where the project will be created.</param>
    /// <returns>A task that represents the asynchronous operation. The task result is <see langword="true"/> if the project was created successfully; otherwise, <see langword="false"/>.</returns>
    public async Task<bool> NewProjectFromTemplate(ITemplateInfo template, string projectName, string location)
    {
        Debug.WriteLine($"New project from template: {template.Category.Name}/{template.Name} with name `{projectName}` in location `{location}`");

        this.preloadedProjects = false; // Refresh recent projects next time we are activated
        this.preloadedTemplates = false; // Refresh recent templates next time we are activated

        var result = await projectBrowser.NewProjectFromTemplate(template, projectName, location).ConfigureAwait(true);
        if (!result)
        {
            return false;
        }

        await router.NavigateAsync("/we", new FullNavigation()).ConfigureAwait(true);

        // TODO: returning a bool here is weird. we should through on error and retrun void
        return true;
    }

    /// <summary>
    /// Opens an existing project.
    /// </summary>
    /// <param name="projectInfo">The information of the project to open.</param>
    /// <returns>A task that represents the asynchronous operation. The task result is <see langword="true"/> if the project was opened successfully; otherwise, <see langword="false"/>.</returns>
    public async Task<bool> OpenProjectAsync(IProjectInfo projectInfo)
    {
        Debug.WriteLine($"Opening project with name `{projectInfo.Name}` in location `{projectInfo.Location}`");

        var result = await projectBrowser.OpenProjectAsync(projectInfo).ConfigureAwait(true);
        if (!result)
        {
            return false;
        }

        this.preloadedProjects = false; // Refresh recent projects next time we are activated

        await router.NavigateAsync("/we", new FullNavigation()).ConfigureAwait(true);

        // TODO: returning a bool here is weird. we should through on error and retrun void
        return true;
    }

    /// <summary>
    /// Navigates to the view for more templates.
    /// </summary>
    [RelayCommand]
    private async Task MoreTemplatesAsync()
    {
        if (this.activeRoute is not null)
        {
            await router.NavigateAsync("new", new PartialNavigation() { RelativeTo = this.activeRoute.Parent }).ConfigureAwait(true);
        }
    }

    /// <summary>
    /// Navigates to the view for more projects.
    /// </summary>
    [RelayCommand]
    private async Task MoreProjectsAsync()
    {
        if (this.activeRoute is not null)
        {
            await router.NavigateAsync("open", new PartialNavigation() { RelativeTo = this.activeRoute.Parent }).ConfigureAwait(true);
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "pre-loading happens during route activation and we cannot report exceptions in that stage")]
    private async Task PreloadRecentTemplatesAsync()
    {
        if (this.preloadedTemplates)
        {
            return;
        }

        try
        {
            await this.LoadRecentTemplatesAsync().ConfigureAwait(true);
            this.preloadedTemplates = true;
        }
        catch (Exception ex)
        {
            this.LogPreloadingRecentTemplatesError(ex);
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "pre-loading happens during route activation and we cannot report exceptions in that stage")]
    private async Task PreloadRecentProjectsAsync()
    {
        if (this.preloadedProjects)
        {
            return;
        }

        try
        {
            await this.LoadRecentProjectsAsync().ConfigureAwait(true);
            this.preloadedProjects = true;
        }
        catch (Exception ex)
        {
            this.LogPreloadingRecentProjectsError(ex);
        }
    }

    /// <summary>
    /// Loads the project templates asynchronously.
    /// </summary>
    [RelayCommand]
    private async Task LoadRecentTemplatesAsync()
    {
        this.RecentTemplates.Clear();
        if (await templateService.HasRecentlyUsedTemplatesAsync().ConfigureAwait(true))
        {
            _ = templateService.GetRecentlyUsedTemplates()
                .ObserveOn(hostingContext.DispatcherScheduler)
                .Subscribe(template => this.RecentTemplates.InsertInPlace(template, x => x.LastUsedOn));
        }
        else
        {
            await foreach (var template in templateService.GetLocalTemplatesAsync().ConfigureAwait(true))
            {
                this.RecentTemplates.InsertInPlace(
                    template,
                    x => x.LastUsedOn!,
                    new DateTimeComparerDescending());
            }
        }
    }

    /// <summary>
    /// Loads the recent projects asynchronously.
    /// </summary>
    [RelayCommand]
    private async Task LoadRecentProjectsAsync()
    {
        // Track the recent projects loaded from the project browser service in
        // a dictionary for fast lookup.
        var recentlyUsedProjectDict = new Dictionary<IProjectInfo, IProjectInfo>();

        // Update existing items and add new items in the RecentProjects collection
        await foreach (var projectInfo in projectBrowser.GetRecentlyUsedProjectsAsync().ConfigureAwait(true))
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

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to preload recently used templates during ViewModel activation")]
    private partial void LogPreloadingRecentTemplatesError(Exception ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to preload recently used projects during ViewModel activation")]
    private partial void LogPreloadingRecentProjectsError(Exception ex);
}
