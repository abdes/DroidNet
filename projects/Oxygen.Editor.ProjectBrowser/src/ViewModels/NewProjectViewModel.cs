// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.ViewModels;

using System.Collections.ObjectModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Collections;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Templates;

/// <summary>The ViewModel for the StartNewPage.</summary>
/// <param name="templateService">The template service to be used to access project templates.</param>
/// <param name="projectBrowserService">The project service to be used to access and manipulate projects.</param>
public partial class NewProjectViewModel(
    ITemplatesService templateService,
    IProjectBrowserService projectBrowserService)
    : ObservableObject
{
    [ObservableProperty]
    private ITemplateInfo? selectedItem;

    public ObservableCollection<ITemplateInfo> Templates { get; } = [];

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

    [RelayCommand]
    public void SelectItem(ITemplateInfo item) => this.SelectedItem = item;

    public async Task<bool> NewProjectFromTemplate(ITemplateInfo template, string projectName, string location)
    {
        Debug.WriteLine(
            $"New project from template: {template.Category.Name}/{template.Name} with name `{projectName}` in location `{location}`");

        return await projectBrowserService.NewProjectFromTemplate(template, projectName, location).ConfigureAwait(true);
    }
}
