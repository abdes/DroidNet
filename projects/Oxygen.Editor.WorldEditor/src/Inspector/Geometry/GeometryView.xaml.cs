// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;

namespace Oxygen.Editor.World.Inspector.Geometry;

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

    private async void OnPickerItemClicked(object? sender, RoutedEventArgs e)
    {
        Debug.Assert(this.ViewModel is not null, "ViewModel should not be null when handling picker item click.");

        if (sender is not FrameworkElement fe || fe.DataContext is not AssetPickerItem item)
        {
            return;
        }

        await this.ViewModel.ApplyAssetAsync(item).ConfigureAwait(true);
        this.AssetPickerFlyout.Hide();
    }
}
