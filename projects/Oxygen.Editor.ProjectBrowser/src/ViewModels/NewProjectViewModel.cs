// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Collections;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Templates;

namespace Oxygen.Editor.ProjectBrowser.ViewModels;

/// <summary>
/// The ViewModel for the StartNewPage.
/// </summary>
/// <param name="templateService">The template service to be used to access project templates.</param>
/// <param name="projectBrowserService">The project service to be used to access and manipulate projects.</param>
public partial class NewProjectViewModel(
    ITemplatesService templateService,
    IProjectBrowserService projectBrowserService)
    : ObservableObject
{
    [ObservableProperty]
    private ITemplateInfo? selectedItem;

    /// <summary>
    /// Gets the collection of project templates.
    /// </summary>
    public ObservableCollection<ITemplateInfo> Templates { get; } = [];

    /// <summary>
    /// Loads the project templates.
    /// </summary>
    [RelayCommand]
    public void LoadTemplates()
    {
        this.Templates.Clear();

        _ = templateService.GetLocalTemplatesAsync()
            .ToObservable()
            .Subscribe(
                template => this.Templates.InsertInPlace(template, x => x.LastUsedOn),
                _ =>
                {
                    // Ignore and continue
                },
                () => this.SelectedItem = this.Templates[0]);
    }

    /// <summary>
    /// Selects the specified template item.
    /// </summary>
    /// <param name="item">The template item to select.</param>
    [RelayCommand]
    public void SelectItem(ITemplateInfo item) => this.SelectedItem = item;

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
}
