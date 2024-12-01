// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Collections;
using DroidNet.Routing;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Templates;

namespace Oxygen.Editor.ProjectBrowser.ViewModels;

/// <summary>
/// The ViewModel for the StartNewPage.
/// </summary>
/// <param name="templateService">The template service to be used to access project templates.</param>
/// <param name="projectBrowserService">The project service to be used to access and manipulate projects.</param>
/// <param name="loggerFactory">
/// The <see cref="ILoggerFactory" /> used to obtain an <see cref="ILogger" />. If the logger
/// cannot be obtained, a <see cref="NullLogger" /> is used silently.
/// </param>
public partial class NewProjectViewModel(
    ITemplatesService templateService,
    IProjectBrowserService projectBrowserService,
    ILoggerFactory? loggerFactory = null)
    : ObservableObject, IRoutingAware
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<NewProjectViewModel>() ?? NullLoggerFactory.Instance.CreateLogger<NewProjectViewModel>();

    [ObservableProperty]
    private ITemplateInfo? selectedItem;
    private bool preloaded;

    /// <summary>
    /// Gets the collection of project templates.
    /// </summary>
    public ObservableCollection<ITemplateInfo> Templates { get; } = [];

    /// <inheritdoc/>
    public async Task OnNavigatedToAsync(IActiveRoute route) => await this.PreloadTemplatesAsync().ConfigureAwait(true);

    /// <summary>
    /// Creates a new project from the specified template.
    /// </summary>
    /// <param name="template">The template to use for the new project.</param>
    /// <param name="projectName">The name of the new project.</param>
    /// <param name="location">The location where the project will be created.</param>
    /// <returns>A task that represents the asynchronous operation. The task result is <see langword="true"/> if the project was created successfully; otherwise, <see langword="false"/>.</returns>
    public async Task<bool> NewProjectFromTemplate(ITemplateInfo template, string projectName, string location)
    {
        Debug.WriteLine(
            $"New project from template: {template.Category.Name}/{template.Name} with name `{projectName}` in location `{location}`");

        return await projectBrowserService.NewProjectFromTemplate(template, projectName, location).ConfigureAwait(true);
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "pre-loading happens during route activation and we cannot report exceptions in that stage")]
    private async Task PreloadTemplatesAsync()
    {
        if (this.preloaded)
        {
            return;
        }

        try
        {
            await this.LoadTemplatesAsync().ConfigureAwait(true);
            this.preloaded = true;
        }
        catch (Exception ex)
        {
            this.LogPreloadingTemplatesError(ex);
        }
    }

    /// <summary>
    /// Loads the project templates.
    /// </summary>
    /// <returns>>A <see cref="Task"/> representing the asynchronous operation.</returns>
    [RelayCommand]
    private async Task LoadTemplatesAsync()
    {
        this.Templates.Clear();
        await foreach (var template in templateService.GetLocalTemplatesAsync().ConfigureAwait(true))
        {
            this.Templates.InsertInPlace(
                template,
                x => x.LastUsedOn!,
                new DateTimeComparerDescending());
        }

        if (this.Templates.Count > 0)
        {
            this.SelectedItem = this.Templates[0];
        }
    }

    /// <summary>
    /// Selects the specified template item.
    /// </summary>
    /// <param name="item">The template item to select.</param>
    [RelayCommand]
    private void SelectItem(ITemplateInfo item) => this.SelectedItem = item;

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to preload templates during ViewModel activation")]
    private partial void LogPreloadingTemplatesError(Exception ex);
}
