// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ViewModels;

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Collections;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Services;
using Oxygen.Editor.ProjectBrowser.Storage;
using Oxygen.Editor.ProjectBrowser.Templates;

public partial class MainViewModel : ObservableObject
{
    private readonly IKnownLocationsService knownLocationsService;

    [ObservableProperty]
    private Dictionary<string, KnownLocation?> locations = [];

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(TemplateFullName))]
    private ITemplateInfo? selectedItem;

    [ObservableProperty]
    private KnownLocation? selectedLocation;

    public MainViewModel(ITemplatesService templatesService, IKnownLocationsService knownLocationsService)
    {
        this.knownLocationsService = knownLocationsService;
        _ = templatesService.GetLocalTemplates()
            .Subscribe(template => this.ProjectTemplates.InsertInPlace(template, x => x.LastUsedOn));
    }

    public string TemplateFullName
        => this.SelectedItem != null ? $"{this.SelectedItem.Category?.Name}/{this.SelectedItem.Name}" : "None";

    public ObservableCollection<ITemplateInfo> ProjectTemplates { get; } = new();

    public void SelectLocation(KnownLocation location)
        => this.SelectedLocation = location;

    [RelayCommand]
    private async Task Initialize()
    {
        this.OnPropertyChanging(nameof(this.Locations));

        foreach (var locationKey in Enum.GetValues<KnownLocations>())
        {
            this.Locations[locationKey.ToString()]
                = await this.knownLocationsService.ForKeyAsync(locationKey).ConfigureAwait(false);
        }

        this.OnPropertyChanged(nameof(this.Locations));

        this.SelectedLocation = this.Locations.First().Value;
    }
}
