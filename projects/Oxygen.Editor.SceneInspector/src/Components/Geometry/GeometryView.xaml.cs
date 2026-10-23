// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Pickers;

namespace Oxygen.Editor.SceneInspector.Components.Geometry;

/// <summary>
///     Represents the view for editing geometry properties in the World Editor.
/// </summary>
[ViewModel(typeof(GeometryViewModel))]
public partial class GeometryView
{
    private Flyout? openMaterialPicker;

    /// <summary>
    /// Initializes a new instance of the <see cref="GeometryView"/> class.
    /// </summary>
    public GeometryView()
    {
        this.InitializeComponent();
    }

    private async void OnGeometryChoiceInvoked(object? sender, AssetChoice choice)
    {
        Debug.Assert(this.ViewModel is not null, "ViewModel should not be null when handling a geometry choice.");
        await this.ViewModel.ApplyAssetAsync(choice).ConfigureAwait(true);
        this.AssetPickerFlyout.Hide();
    }

    private async void OnMaterialPickerOpening(object? sender, object args)
    {
        _ = args;
        if (sender is Flyout { Target.DataContext: SurfaceCardViewModel card } flyout)
        {
            this.openMaterialPicker = flyout;
            await card.PrepareMaterialPickerAsync().ConfigureAwait(true);
        }
    }

    private async void OnMaterialChoiceInvoked(object? sender, AssetChoice choice)
    {
        if (sender is FrameworkElement { DataContext: SurfaceCardViewModel card })
        {
            this.openMaterialPicker?.Hide();
            this.openMaterialPicker = null;
            await card.ApplyMaterialAsync(choice).ConfigureAwait(true);
        }
    }
}
