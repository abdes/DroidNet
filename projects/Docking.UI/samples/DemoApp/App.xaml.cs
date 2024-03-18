// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Demo;

using System.Diagnostics.CodeAnalysis;
using DryIoc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;

/// <summary>Provides application-specific behavior to supplement the default Application class.</summary>
[ExcludeFromCodeCoverage]
public partial class App
{
    private readonly IResolver resolver;
    private readonly IValueConverter vmToViewConverter;
    private Window? window;

    /// <summary>Initializes a new instance of the <see cref="App" /> class.</summary>
    /// In this project architecture, the single instance of the application is created by the User Interface hosted service as
    /// part of the application host initialization. Its lifecycle is managed together with the rest of the services.
    /// <param name="resolver">The Dependency Injector's service provider.</param>
    public App(IResolver resolver, [FromKeyedServices("VmToView")] IValueConverter converter)
    {
        this.resolver = resolver;
        this.vmToViewConverter = converter;

        this.InitializeComponent();
    }

    /// <summary>Invoked when the application is launched.</summary>
    /// <param name="args">Details about the launch request and process.</param>
    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        Current.Resources["VmToViewConverter"] = this.vmToViewConverter;

        // Create and activate the application main window.
        this.window = this.resolver.Resolve<MainWindow>();
        this.window.Activate();
    }
}
