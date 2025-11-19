// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura;

/// <summary>
///     A <see cref="Window"/>, that can be the target of a routed navigation. Acts as an <see
///     cref="IOutletContainer"/>, with a single outlet that can host the window's main content.
/// </summary>
/// <seealso cref="MainShellViewModel"/>
/// <seealso cref="IOutletContainer"/>
/// <seealso cref="IRouter"/>
public sealed partial class RoutedWindow : IOutletContainer, INotifyPropertyChanged
{
    private object? contentViewModel;

    /// <summary>
    /// Initializes a new instance of the <see cref="RoutedWindow" /> class.
    /// </summary>
    /// <remarks>
    ///     This window is created and activated when the application is launched. This approach
    ///     ensures that window creation and destruction are managed by the application itself. This
    ///     is crucial in applications where multiple windows exist, as it might not be clear which
    ///     window is the main one, which impacts the UI lifetime. The window does not have a view
    ///     model and does not need one. Windows are solely responsible for window-specific tasks,
    ///     while the 'shell' view inside the window handles loading the appropriate content based
    ///     on the active route or state of the application.
    /// </remarks>
    public RoutedWindow()
    {
        this.InitializeComponent();

        /*
        var workArea = DisplayArea.Primary.WorkArea;
        this.AppWindow.MoveAndResize(
            new RectInt32((workArea.Width - width) / 2, (workArea.Height - height) / 2, width, height),
            DisplayArea.Primary);
        */
    }

    /// <summary>
    /// Event raised when a property value changes.
    /// </summary>
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <summary>
    /// Gets or sets the content view model.
    /// </summary>
    public object? ContentViewModel
    {
        get => this.contentViewModel;
        set
        {
            if (!Equals(this.contentViewModel, value))
            {
                this.contentViewModel = value;
                this.OnPropertyChanged(nameof(this.ContentViewModel));
            }
        }
    }

    /// <inheritdoc />
    public void LoadContent(object viewModel, OutletName? outletName = null)
    {
        if (this.ContentViewModel != viewModel)
        {
            if (this.ContentViewModel is IDisposable resource)
            {
                resource.Dispose();
            }

            this.ContentViewModel = viewModel;
        }
    }

    /// <summary>
    /// Raises the PropertyChanged event.
    /// </summary>
    /// <param name="propertyName">The name of the property that changed.</param>
    private void OnPropertyChanged(string propertyName)
        => this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}
