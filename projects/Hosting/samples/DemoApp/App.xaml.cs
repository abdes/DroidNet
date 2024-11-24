// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml;
using Windows.Graphics;

namespace DroidNet.Hosting.Demo;

/// <summary>
/// Provides application-specific behavior to supplement the default
/// Application class.
/// </summary>
[ExcludeFromCodeCoverage]
[SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "Application class must be public")]
public partial class App
{
    private readonly IServiceProvider serviceProvider;
    private Window? window;

    /// <summary>
    /// Initializes a new instance of the <see cref="App" /> class.
    /// </summary>
    /// In this project architecture, the single instance of the application is
    /// created by the User Interface hosted service as part of the application
    /// host initialization. Its lifecycle is managed together with the rest of
    /// the services.
    /// <param name="serviceProvider">
    /// The Dependency Injector's service provider.
    /// </param>
    public App(IServiceProvider serviceProvider)
    {
        this.serviceProvider = serviceProvider;
        this.InitializeComponent();
    }

    /// <summary>Invoked when the application is launched.</summary>
    /// <param name="args">
    /// Details about the launch request and process.
    /// </param>
    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        /*
         * At a minimum we should create the application main window here.
         * This is not different from any regular WinUI application and has no
         * specific requirements due to the hosting.
         */

        this.window = ActivatorUtilities.CreateInstance<MainWindow>(this.serviceProvider);
        this.window.AppWindow.Resize(new SizeInt32(1600, 900));
        this.window.Activate();
    }
}
