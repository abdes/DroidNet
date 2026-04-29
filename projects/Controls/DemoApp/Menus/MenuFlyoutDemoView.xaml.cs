// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;

namespace DroidNet.Controls.Demo.Menus;

/// <summary>
/// View for MenuFlyout demonstration.
/// </summary>
[ViewModel(typeof(MenuFlyoutDemoViewModel))]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "must be public due to source generated ViewModel property")]
public sealed partial class MenuFlyoutDemoView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="MenuFlyoutDemoView"/> class.
    /// </summary>
    public MenuFlyoutDemoView()
    {
        this.InitializeComponent();
        this.Loaded += this.OnLoaded;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        _ = sender;
        _ = e;

        if (this.ViewModel is { } viewModel &&
            this.Resources["MenuNumberBoxItemTemplate"] is DataTemplate numberBoxTemplate &&
            this.Resources["MenuSliderItemTemplate"] is DataTemplate sliderTemplate &&
            this.Resources["MenuToggleItemTemplate"] is DataTemplate toggleTemplate)
        {
            viewModel.ApplyInteractiveContentTemplates(numberBoxTemplate, sliderTemplate, toggleTemplate);
        }
    }
}
