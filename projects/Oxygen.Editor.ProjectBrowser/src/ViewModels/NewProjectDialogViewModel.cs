// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Templates;

namespace Oxygen.Editor.ProjectBrowser.ViewModels;

/// <summary>
/// ViewModel for the New Project dialog in the Oxygen Editor's Project Browser.
/// </summary>
public partial class NewProjectDialogViewModel : ObservableObject
{
    [ObservableProperty]
    private IList<QuickSaveLocation> pinnedLocations;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsProjectNameValid))]
    private string projectName;

    [ObservableProperty]
    private QuickSaveLocation selectedLocation;

    [ObservableProperty]
    private ITemplateInfo template;

    /// <summary>
    /// Initializes a new instance of the <see cref="NewProjectDialogViewModel"/> class.
    /// </summary>
    /// <param name="projectBrowser">The project browser service.</param>
    /// <param name="template">The template information for the new project.</param>
    public NewProjectDialogViewModel(IProjectBrowserService projectBrowser, ITemplateInfo template)
    {
        this.Template = template;

        this.PinnedLocations = projectBrowser.GetQuickSaveLocations();
        this.SelectedLocation = this.PinnedLocations[0];

        this.ProjectName = string.Empty;
    }

    /// <summary>
    /// Gets a value indicating whether the project name is valid.
    /// </summary>
    public bool IsProjectNameValid
        => !string.IsNullOrEmpty(this.ProjectName); // TODO: validate project name, use CanCreateProject

    /// <summary>
    /// Sets the selected location.
    /// </summary>
    /// <param name="location">The location to set as selected.</param>
    [RelayCommand]
    private void SetLocation(QuickSaveLocation location)
        => this.SelectedLocation = location;
}
