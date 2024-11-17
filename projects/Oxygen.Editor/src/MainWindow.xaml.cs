// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Mvvm;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;
using Oxygen.Editor.Helpers;

namespace Oxygen.Editor;

/// <summary>The application main window, which also acts as a <see cref="IOutletContainer" /> for some routes.</summary>
[ObservableObject]
public sealed partial class MainWindow : IOutletContainer
{
    [ObservableProperty]
    private object? contentViewModel;

    /// <summary>
    /// Initializes a new instance of the <see cref="MainWindow"/> class.
    /// </summary>
    public MainWindow(IViewLocator viewLocator)
    {
        this.InitializeComponent();

        this.AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory, "Assets/WindowIcon.ico"));
        this.Title = "AppDisplayName".GetLocalized();

        /* TODO: refactor theme management
        // Theme change code picked from https://github.com/microsoft/WinUI-Gallery/pull/1239
        this.dispatcherQueue = Microsoft.UI.Dispatching.DispatcherQueue.GetForCurrentThread();
        this.settings = new UISettings();
        this.settings.ColorValuesChanged
            += this.Settings_ColorValuesChanged; // cannot use FrameworkElement.ActualThemeChanged event
        */
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
