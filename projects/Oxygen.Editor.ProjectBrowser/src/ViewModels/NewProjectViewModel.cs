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
using Microsoft.Extensions.DependencyInjection;
using Oxygen.Editor.ProjectBrowser.Services;
using Oxygen.Editor.ProjectBrowser.Templates;

/// <summary>The ViewModel for the StartNewPage.</summary>
[InjectAs(ServiceLifetime.Singleton)]
public partial class NewProjectViewModel : ObservableObject
{
    private readonly IProjectsService projectsService;
    private readonly ITemplatesService templateService;

    [ObservableProperty]
    private ITemplateInfo? selectedItem;

    public NewProjectViewModel(ITemplatesService templateService, IProjectsService projectsService)
    {
        this.templateService = templateService;
        this.projectsService = projectsService;
    }

    public ObservableCollection<ITemplateInfo> Templates { get; } = new();

    [RelayCommand]
    public void LoadTemplates()
    {
        this.Templates.Clear();

        _ = this.templateService.GetLocalTemplates()
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
            $"New project from template: {template.Category!.Name}/{template.Name} with name `{projectName}` in location `{location}`");

        return await this.projectsService.NewProjectFromTemplate(template, projectName, location).ConfigureAwait(true);
    }
}
