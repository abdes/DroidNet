// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;

namespace Oxygen.Editor.WorldEditor.Inspector;

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
        if (sender is not FrameworkElement fe)
        {
            return;
        }

        if (fe.DataContext is not GeometryAssetPickerItem item)
        {
            return;
        }

        if (this.ViewModel is null)
        {
            return;
        }

        try
        {
            await this.ViewModel.ApplyAssetAsync(item).ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[GeometryView] Error applying asset: {ex.Message}");
        }

        // Close the flyout if possible
        try
        {
            this.AssetPickerFlyout?.Hide();
        }
        catch (Exception)
        {
            // ignore
        }
    }
}
