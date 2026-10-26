// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Oxygen.Editor.Pickers;

namespace Oxygen.Editor.SceneInspector.Components.Geometry;

/// <summary>
///     Represents the view for editing geometry properties in the World Editor.
/// </summary>
[ViewModel(typeof(GeometryViewModel))]
public partial class GeometryView
{
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
    }

    private async void OnMaterialPickerOpening(object? sender, EventArgs args)
    {
        _ = args;
        if (sender is FrameworkElement { DataContext: SurfaceCardViewModel card })
        {
            await card.PrepareMaterialPickerAsync().ConfigureAwait(true);
        }
    }

    private async void OnMaterialChoiceInvoked(object? sender, AssetChoice choice)
    {
        if (sender is FrameworkElement { DataContext: SurfaceCardViewModel card })
        {
            await card.ApplyMaterialAsync(choice).ConfigureAwait(true);
        }
    }
}
