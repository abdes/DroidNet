// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Samples.WinPackagedApp;

using Microsoft.UI.Xaml;

/// <summary>
/// Provides application-specific behavior to supplement the default
/// Application class.
/// </summary>
public partial class App
{
    private Window? window;

    /// <summary>
    /// Initializes a new instance of the <see cref="App" /> class.
    /// </summary>
    public App() => this.InitializeComponent();

    /// <summary>Invoked when the application is launched.</summary>
    /// <param name="args">
    /// Details about the launch request and process.
    /// </param>
    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        this.window = new MainWindow();
        this.window.AppWindow.MoveAndResize(new Windows.Graphics.RectInt32(500, 500, 800, 600));
        this.window.Activate();
    }
}
