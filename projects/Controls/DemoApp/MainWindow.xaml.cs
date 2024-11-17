// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;

/* ReSharper disable PrivateFieldCanBeConvertedToLocalVariable */

namespace DroidNet.Controls.Demo;

/// <summary>The User Interface's main window.</summary>
/// <remarks>
/// <para>
/// This window is created and activated when the Application is Launched. This is preferred to the alternative of doing that in
/// the hosted service to keep the control of window creation and destruction under the application itself. Not all applications
/// have a single window, and it is often not obvious which window is considered the main window, which is important in
/// determining when the UI lifetime ends.
/// </para>
/// <para>
/// The window does not have a view model and does not need one. The design principle is that windows are here only to do window
/// stuff and the content inside the window is provided by a 'shell' view that will in turn load the appropriate content based on
/// the application active route or state.
/// </para>
/// </remarks>
[ExcludeFromCodeCoverage]
[ObservableObject]
public sealed partial class MainWindow : IOutletContainer
{
    [ObservableProperty]
    private object? contentViewModel;

    /// <summary>
    /// Initializes a new instance of the <see cref="MainWindow"/> class.
    /// </summary>
    public MainWindow()
    {
        this.InitializeComponent();

        // this.AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory, "Assets/WindowIcon.ico"));
        // this.Title = "AppDisplayName".GetLocalized();
    }

    /// <inheritdoc/>
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
