// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Routing.WinUI;
using Microsoft.UI.Xaml;

namespace DroidNet.Routing.Demo;

/// <summary>The application main window, which also acts as a <see cref="IOutletContainer" /> for some routes.</summary>
[ObservableObject]
internal sealed partial class MainWindow : IOutletContainer
{
    [ObservableProperty]
    private object? contentViewModel;

    /// <summary>Initializes a new instance of the <see cref="MainWindow" /> class.</summary>
    /// <remarks>
    /// <para>
    /// This window is created and activated when the <see cref="Application" /> is Launched. This is preferred to the alternative
    /// of doing that in the hosted service to keep the control of window creation and destruction under the application itself.
    /// Not all applications have a single window, and it is often not obvious which window is considered the main window, which
    /// is important in determining when the UI lifetime ends.
    /// </para>
    /// <para>
    /// The window does not have a view model and does not need one. The design principle is that windows are here only to do
    /// window stuff and the content inside the window is provided by a 'shell' view that will in turn load the appropriate
    /// content based on the application active route or state.
    /// </para>
    /// </remarks>
    public MainWindow()
    {
        this.InitializeComponent();
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
}
