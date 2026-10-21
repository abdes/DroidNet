// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Oxygen.Editor.World.Inspector.Presentation;

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>Composes the parent's canonical source editors and independent source actions.</summary>
[ViewModel(typeof(AtmosphereLightsSectionViewModel))]
public sealed partial class AtmosphereLightsSectionView
{
    /// <summary>Initializes a new instance of the <see cref="AtmosphereLightsSectionView"/> class.</summary>
    public AtmosphereLightsSectionView() => this.InitializeComponent();

    /// <summary>Registers source presentation without replacing the canonical Light editors.</summary>
    /// <param name="registry">The host's presentation registry.</param>
    internal void Register(InspectorSceneFieldRegistry registry)
    {
        registry.Section("AtmosphereLights", this, this.AtmosphereLightsSection);
        registry.Field("Sources", this.SourcesCard);
        registry.Sources.Add("PrimarySource", this.PrimarySourceDisclosure);
        registry.Sources.Add("SecondarySource", this.SecondarySourceDisclosure);
    }

    private async void ResetAtmosphereSources_Click(object sender, RoutedEventArgs args)
    {
        if (this.ViewModel is { } model)
        {
            await model.ResetAtmosphereSourcesAsync().ConfigureAwait(true);
        }
    }

    private async void InspectPrimarySource_Click(object sender, RoutedEventArgs args)
    {
        if (this.ViewModel is { } model)
        {
            await model.InspectAtmosphereSourceAsync(model.SelectedSun).ConfigureAwait(true);
        }
    }

    private async void InspectSecondarySource_Click(object sender, RoutedEventArgs args)
    {
        if (this.ViewModel is { } model)
        {
            await model.InspectAtmosphereSourceAsync(model.SelectedSecondarySun).ConfigureAwait(true);
        }
    }
}
