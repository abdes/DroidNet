// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Generators;

using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.Mvvm.DependencyInjection;
using DroidNet.Routing.Generators.Demo;
using DroidNet.Routing.Generators.ViewModels;
using DroidNet.Routing.View;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.UI.Xaml;
using Microsoft.VisualStudio.TestTools.UnitTesting.AppContainer;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

/// <summary>
/// Provides application-specific behavior to supplement the default Application
/// class.
/// </summary>
[ExcludeFromCodeCoverage]
public partial class App
{
    /// <summary>
    /// Initializes a new instance of the <see cref="App" /> class.
    /// </summary>
    public App() => this.InitializeComponent();

    /// <summary>
    /// Invoked when the application is launched normally by the end user.  Other entry
    /// points
    /// will be used such as when the application is launched to open a specific file.
    /// </summary>
    /// <param name="args">Details about the launch request and process.</param>
    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        var host = Host.CreateDefaultBuilder()
            .ConfigureServices(
                (_, services) => services.AddTransient<DemoViewModel>()
                    .AddTransient<IViewFor<DemoViewModel>, DemoView>())
            .Build();

        Ioc.Default.ConfigureServices(host.Services);

        Microsoft.VisualStudio.TestPlatform.TestExecutor.UnitTestClient.CreateDefaultUI();

        // Ensure the current window is active
        var window = new MainWindow();
        window.Activate();
        UITestMethodAttribute.DispatcherQueue = window.DispatcherQueue;

        // Replace back with e.Arguments when https://github.com/microsoft/microsoft-ui-xaml/issues/3368 is fixed
        Microsoft.VisualStudio.TestPlatform.TestExecutor.UnitTestClient.Run(Environment.CommandLine);
    }
}
