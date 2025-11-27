// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
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
using Oxygen.Editor.World;

namespace Oxygen.Editor.ProjectBrowser.ViewModels;

/// <summary>
///     ViewModel for the Home view in the Oxygen Editor's Project Browser.
/// </summary>
/// <param name="router">The router for navigating between views.</param>
/// <param name="templateService">The service for managing project templates.</param>
/// <param name="projectBrowser">The service for managing projects.</param>
/// <param name="loggerFactory">
///     The <see cref="ILoggerFactory" /> used to obtain an <see cref="ILogger" />. If the logger
///     cannot be obtained, a <see cref="NullLogger" /> is used silently.
/// </param>
public partial class HomeViewModel(
    HostingContext hostingContext,
    IRouter router,
    ITemplatesService templateService,
    IProjectBrowserService projectBrowser,
    ILoggerFactory? loggerFactory = null) : ObservableObject, IRoutingAware
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<HomeViewModel>() ?? NullLoggerFactory.Instance.CreateLogger<HomeViewModel>();

    private IActiveRoute? activeRoute;
    private bool preloadedTemplates;
    private bool preloadedProjects;

    [ObservableProperty]
    public partial ITemplateInfo? SelectedTemplate { get; set; }

    /// <summary>
    /// Gets the collection of project templates.
    /// </summary>
    public ObservableCollection<ITemplateInfo> RecentTemplates { get; } = [];

    /// <summary>
    /// Gets the collection of recent projects.
    /// </summary>
    public ObservableCollection<IProjectInfo> RecentProjects { get; } = [];

    /// <summary>
    /// Gets the <see cref="ILoggerFactory"/> used to obtain an <see cref="ILogger"/> instance for logging.
    /// </summary>
    internal ILoggerFactory? LoggerFactory => loggerFactory;

    /// <inheritdoc/>
    public async Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
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
        this.LogNewProjectFromTemplate(template.Category.Name, template.Name, projectName, location);

        this.preloadedProjects = false; // Refresh recent projects next time we are activated
        this.preloadedTemplates = false; // Refresh recent templates next time we are activated

        var result = await projectBrowser.NewProjectFromTemplate(template, projectName, location).ConfigureAwait(true);
        if (!result)
        {
            return false;
        }

        await router.NavigateAsync("/we", new FullNavigation() { Target = new Target { Name = "wnd-we" }, ReplaceTarget = true }).ConfigureAwait(true);

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
        this.LogOpenProject(projectInfo.Name, projectInfo.Location ?? string.Empty);

        var result = await projectBrowser.OpenProjectAsync(projectInfo).ConfigureAwait(true);
        if (!result)
        {
            return false;
        }

        this.preloadedProjects = false; // Refresh recent projects next time we are activated

        await router.NavigateAsync("/we", new FullNavigation()
        {
            Target = new Target { Name = "wnd-we" },
            ReplaceTarget = true,
        }).ConfigureAwait(true);

        // TODO: returning a bool here is weird. we should through on error and retrun void
        return true;
    }

    /// <summary>
    /// Navigates to the view for more templates.
    /// </summary>
    [RelayCommand]
    private async Task MoreTemplatesAsync()
        => await router.NavigateAsync("/pb/new").ConfigureAwait(true);

    /// <summary>
    /// Navigates to the view for more projects.
    /// </summary>
    [RelayCommand]
    private async Task MoreProjectsAsync()
        => await router.NavigateAsync("/pb/open").ConfigureAwait(true);

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
        this.LogUpdatingRecentProjects(this.RecentProjects.Count);

        // Track the recent projects loaded from the project browser service in
        // a dictionary keyed by project Id for fast lookup. Using Guid avoids
        // accidental reference-equality comparisons when the dictionary is
        // typed against the interface `IProjectInfo`.
        var recentlyUsedProjectDict = new Dictionary<Guid, IProjectInfo>();

        // Update existing items and add new items in the RecentProjects collection
        await foreach (var projectInfo in projectBrowser.GetRecentlyUsedProjectsAsync().ConfigureAwait(true))
        {
            this.LogGotProjectInfo(projectInfo.Id, projectInfo.Name, projectInfo.LastUsedOn);
            recentlyUsedProjectDict.Add(projectInfo.Id, projectInfo);

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
            if (!recentlyUsedProjectDict.ContainsKey(this.RecentProjects[index].Id))
            {
                this.LogPurgeRecentProjectItem(this.RecentProjects[index].Id, this.RecentProjects[index].Name);
                this.RecentProjects.RemoveAt(index);
            }
        }

        this.LogCompletedLoadingRecentProjects(this.RecentProjects.Count);
    }
}
