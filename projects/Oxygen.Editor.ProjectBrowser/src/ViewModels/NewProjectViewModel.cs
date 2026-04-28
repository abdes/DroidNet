// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Routing;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.ProjectBrowser.Activation;
using Oxygen.Editor.ProjectBrowser.Templates;

namespace Oxygen.Editor.ProjectBrowser.ViewModels;

/// <summary>
/// The ViewModel for the StartNewPage.
/// </summary>
/// <param name="templateService">The template service to be used to access project templates.</param>
/// <param name="activationCoordinator">The shell coordinator that activates projects into the workspace.</param>
/// <param name="loggerFactory">
/// The <see cref="ILoggerFactory" /> used to obtain an <see cref="ILogger" />. If the logger
/// cannot be obtained, a <see cref="NullLogger" /> is used silently.
/// </param>
public partial class NewProjectViewModel(
    ITemplatesService templateService,
    IProjectActivationCoordinator activationCoordinator,
    ILoggerFactory? loggerFactory = null)
    : ObservableObject, IRoutingAware
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<NewProjectViewModel>() ?? NullLoggerFactory.Instance.CreateLogger<NewProjectViewModel>();

    private bool preloaded;

    [ObservableProperty]
    public partial ITemplateInfo? SelectedItem { get; set; }

    [ObservableProperty]
    public partial bool IsActivating { get; set; }

    [ObservableProperty]
    public partial bool IsOperationResultVisible { get; set; }

    [ObservableProperty]
    public partial string OperationResultTitle { get; set; } = string.Empty;

    [ObservableProperty]
    public partial string OperationResultMessage { get; set; } = string.Empty;

    [ObservableProperty]
    public partial InfoBarSeverity OperationResultSeverity { get; set; } = InfoBarSeverity.Informational;

    /// <summary>
    /// Gets the collection of project templates.
    /// </summary>
    public ObservableCollection<ITemplateInfo> Templates { get; } = [];

    /// <inheritdoc/>
    public async Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
        => await this.PreloadTemplatesAsync().ConfigureAwait(true);

    /// <summary>
    /// Creates a new project from the specified template.
    /// </summary>
    /// <param name="template">The template to use for the new project.</param>
    /// <param name="projectName">The name of the new project.</param>
    /// <param name="location">The location where the project will be created.</param>
    /// <returns>A task that represents the asynchronous operation. The task result is <see langword="true"/> if the project was created successfully; otherwise, <see langword="false"/>.</returns>
    public async Task<bool> NewProjectFromTemplate(ITemplateInfo template, string projectName, string location)
    {
        this.LogNewProjectFromTemplate(template, projectName, location);

        this.IsActivating = true;

        var result = await activationCoordinator.ActivateAsync(
                new ProjectActivationRequest
                {
                    Mode = ProjectActivationMode.CreateFromTemplate,
                    SourceSurface = ProjectActivationSourceSurface.New,
                    TemplateLocation = template.Location,
                    TemplateId = template.TemplateId ?? template.Name,
                    ProjectName = projectName,
                    ParentLocation = location,
                    Category = template.Category,
                    Thumbnail = template.Icon,
                })
            .ConfigureAwait(true);
        this.ApplyOperationResult(result);
        if (result.Status is OperationStatus.Failed or OperationStatus.Cancelled)
        {
            this.LogNewProjectFailed();
            this.IsActivating = false;
            return false;
        }

        return true;
    }

    /// <summary>
    /// Resets the activation state to allow further project operations.
    /// </summary>
    internal void ResetActivationState() => this.IsActivating = false;

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
#pragma warning disable CA1031 // Do not catch general exception types
        catch (Exception ex)
        {
            this.LogPreloadingTemplatesError(ex);
        }
#pragma warning restore CA1031 // Do not catch general exception types
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
            this.Templates.Add(template);
        }

        // Select the first template by default, if available
        if (this.Templates.Count > 0)
        {
            this.SelectedItem = this.Templates[0];
        }
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
