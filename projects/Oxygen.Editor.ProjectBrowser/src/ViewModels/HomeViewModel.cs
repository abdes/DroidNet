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
using Microsoft.UI.Xaml.Controls;
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.ProjectBrowser.Activation;
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
    IProjectActivationCoordinator activationCoordinator,
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
    public ObservableCollection<RecentProjectEntry> RecentProjects { get; } = [];

    [ObservableProperty]
    public partial bool IsOperationResultVisible { get; set; }

    [ObservableProperty]
    public partial string OperationResultTitle { get; set; } = string.Empty;

    [ObservableProperty]
    public partial string OperationResultMessage { get; set; } = string.Empty;

    [ObservableProperty]
    public partial InfoBarSeverity OperationResultSeverity { get; set; } = InfoBarSeverity.Informational;

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

        var result = await activationCoordinator.ActivateAsync(
                new ProjectActivationRequest
                {
                    Mode = ProjectActivationMode.CreateFromTemplate,
                    SourceSurface = ProjectActivationSourceSurface.Home,
                    TemplateLocation = template.Location,
                    TemplateId = template.Name,
                    ProjectName = projectName,
                    ParentLocation = location,
                    Category = template.Category,
                    Thumbnail = template.Icon,
                })
            .ConfigureAwait(true);
        this.ApplyOperationResult(result);

        // TODO: returning a bool here is weird. we should through on error and retrun void
        return result.Status is not OperationStatus.Failed and not OperationStatus.Cancelled;
    }

    /// <summary>
    /// Opens an existing project.
    /// </summary>
    /// <param name="entry">The recent project entry to open.</param>
    /// <returns>A task that represents the asynchronous operation. The task result is <see langword="true"/> if the project was opened successfully; otherwise, <see langword="false"/>.</returns>
    public async Task<bool> OpenProjectAsync(RecentProjectEntry entry)
    {
        this.LogOpenProject(entry.Name, entry.Location);

        if (string.IsNullOrWhiteSpace(entry.Location))
        {
            return false;
        }

        var result = await activationCoordinator.ActivateAsync(
                new ProjectActivationRequest
                {
                    Mode = ProjectActivationMode.OpenExisting,
                    SourceSurface = ProjectActivationSourceSurface.Home,
                    ProjectLocation = entry.Location,
                    RecentEntryId = entry.Location,
                })
            .ConfigureAwait(true);
        this.ApplyOperationResult(result);

        // TODO: returning a bool here is weird. we should through on error and retrun void
        return result.Status is not OperationStatus.Failed and not OperationStatus.Cancelled;
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
        var recentlyUsedProjectDict = new Dictionary<string, RecentProjectEntry>(StringComparer.OrdinalIgnoreCase);

        // Update existing items and add new items in the RecentProjects collection
        await foreach (var entry in projectBrowser.GetRecentlyUsedProjectsAsync().ConfigureAwait(true))
        {
            var projectInfo = entry.ProjectInfo;
            this.LogGotProjectInfo(projectInfo?.Id ?? Guid.Empty, entry.Name, entry.LastUsedOn);
            recentlyUsedProjectDict[entry.Location] = entry;

            var existingItem = this.RecentProjects.FirstOrDefault(item => string.Equals(item.Location, entry.Location, StringComparison.OrdinalIgnoreCase));
            if (existingItem != null)
            {
                var index = this.RecentProjects.IndexOf(existingItem);
                this.RecentProjects[index] = entry;
            }
            else
            {
                this.RecentProjects.Add(entry);
            }
        }

        // Remove items not in the collection obtained from the project browser
        for (var index = this.RecentProjects.Count - 1; index >= 0; index--)
        {
            if (!recentlyUsedProjectDict.ContainsKey(this.RecentProjects[index].Location))
            {
                this.LogPurgeRecentProjectItem(this.RecentProjects[index].ProjectInfo?.Id ?? Guid.Empty, this.RecentProjects[index].Name);
                this.RecentProjects.RemoveAt(index);
            }
        }

        this.LogCompletedLoadingRecentProjects(this.RecentProjects.Count);
    }

    private void ApplyOperationResult(OperationResult result)
    {
        this.OperationResultTitle = result.Title;
        this.OperationResultMessage = result.Message;
        this.OperationResultSeverity = result.Status switch
        {
            OperationStatus.Succeeded => InfoBarSeverity.Success,
            OperationStatus.SucceededWithWarnings or OperationStatus.PartiallySucceeded => InfoBarSeverity.Warning,
            OperationStatus.Cancelled => InfoBarSeverity.Informational,
            _ => InfoBarSeverity.Error,
        };
        this.IsOperationResultVisible = result.Status is not OperationStatus.Succeeded;
    }
}
