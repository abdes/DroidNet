// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;

namespace DroidNet.Coordinates.PointerDemo;

/// <summary>
///     View hosting the pointer coordinate demonstration content.
/// </summary>
[ViewModel(typeof(DemoViewModel))]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "A View must be public")]
public sealed partial class DemoView
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="DemoView" /> class.
    /// </summary>
    public DemoView()
    {
        this.InitializeComponent();
        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
    }

    private void OnLoaded(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;

        // Ensure DataContext is the generated ViewModel so XAML bindings target it.
        if (this.DataContext is null && this.ViewModel is not null)
        {
            this.DataContext = this.ViewModel;
        }

        this.ViewModel?.StartTracking(this.CoordinateSurface);
    }

    private void OnUnloaded(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;

        this.ViewModel?.StopTracking();
    }
}
