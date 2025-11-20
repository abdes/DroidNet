// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Mvvm.Generators;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Oxygen.Editor.ProjectBrowser.Controls;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.ProjectBrowser.ViewModels;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ProjectBrowser.Views;

/// <summary>
///     Start Home page.
/// </summary>
[ViewModel(typeof(HomeViewModel))]
public sealed partial class HomeView
{
    private readonly ILogger logger;
    private readonly IProjectBrowserService projectBrowser;
    private readonly RecentProjectsListViewModel? recentProjectsListViewModel;

    /// <summary>
    ///     Initializes a new instance of the <see cref="HomeView"/> class.
    /// </summary>
    /// <param name="projectBrowser">The project browser service.</param>
    /// <param name="loggerFactory">
    ///     The <see cref="ILoggerFactory" /> used to obtain an <see cref="ILogger" />. If the logger
    ///     cannot be obtained, a <see cref="NullLogger" /> is used silently.
    /// </param>
    public HomeView(IProjectBrowserService projectBrowser, ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<HomeView>() ?? NullLoggerFactory.Instance.CreateLogger<HomeView>();

        this.projectBrowser = projectBrowser;
        this.InitializeComponent();

        // Setup the RecentProjectsList control with its ViewModel
        var defaultThumbnail = $"ms-appx:///{typeof(ProjectInfo).Assembly.GetName().Name}/Data/Images/DefaultProjectIcon.png";
        this.recentProjectsListViewModel = new RecentProjectsListViewModel(this.projectBrowser, defaultThumbnail, loggerFactory);
        this.RecentProjectsListControl.ViewModel = this.recentProjectsListViewModel;

        // Wire up the HomeViewModel's DataContextChanged to sync RecentProjects
        this.Loaded += this.OnHomeViewLoaded;
    }

    private void OnHomeViewLoaded(object sender, Microsoft.UI.Xaml.RoutedEventArgs e)
    {
        Debug.Assert(this.ViewModel is not null, "[HomeView] ViewModel should not be null.");
        Debug.Assert(this.recentProjectsListViewModel is not null, "[HomeView] recentProjectsListViewModel should not be null.");

        // Set the HomeViewModel's RecentProjects collection directly.
        // The RecentProjectsListViewModel will transform and display items as they're added/removed.
        this.recentProjectsListViewModel.SetRecentProjects(this.ViewModel.RecentProjects);
    }

    private async void OnRecentProjectOpenAsync(object sender, ProjectItemActivatedEventArgs args)
    {
        Debug.Assert(this.ViewModel is not null, "[HomeView] ViewModel should not be null.");
        Debug.Assert(this.recentProjectsListViewModel is not null, "[HomeView] recentProjectsListViewModel should not be null.");

        try
        {
            var success = await this.ViewModel.OpenProjectAsync(args.ProjectInfo).ConfigureAwait(true);
            if (!success)
            {
                this.LogFailedToOpenProject(args.ProjectInfo.Name);

                // TODO: Show error and reset activation state
                this.recentProjectsListViewModel.ResetActivationState();
            }

            // If success, the router will navigate away and close this window, so no need to reset
        }
#pragma warning disable CA1031 // Do not catch general exception types
        catch (Exception ex)
        {
            this.LogFailedToOpenProject(args.ProjectInfo.Name, ex);
            this.recentProjectsListViewModel.ResetActivationState();
        }
#pragma warning restore CA1031 // Do not catch general exception types
    }

    private async void OnNewProjectFromTemplateAsync(object? sender, ItemClickEventArgs args)
    {
        Debug.Assert(this.ViewModel is not null, "[HomeView] ViewModel should not be null.");

        if (args.ClickedItem is not ITemplateInfo templateInfo)
        {
            return;
        }

        var dialog = new NewProjectDialog(this.projectBrowser, templateInfo) { XamlRoot = this.XamlRoot };
        var result = await dialog.ShowAsync();
        if (result == ContentDialogResult.Primary)
        {
            var success = await this.ViewModel.NewProjectFromTemplate(
                    templateInfo,
                    dialog.ViewModel.ProjectName,
                    dialog.ViewModel.SelectedLocation.Path)
                .ConfigureAwait(true);

            if (!success)
            {
                // TODO: display an error message
                this.LogFailedToCreateProjectFromTemplate();
            }
        }
    }

    private async void OnHomeViewKeyDown(object sender, KeyRoutedEventArgs args)
    {
        Debug.Assert(this.ViewModel is not null, "[HomeView] ViewModel should not be null.");

        if (args.Key == Windows.System.VirtualKey.F5)
        {
            await this.ViewModel.LoadRecentTemplatesCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
            await this.ViewModel.LoadRecentProjectsCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        }
    }
}
