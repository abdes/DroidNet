// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Reactive.Linq;
using DroidNet.Aura.Decoration;
using DroidNet.Aura.WindowManagement;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Samples.WinPackagedApp;

/// <summary>
/// Provides application-specific behavior for the multi-window sample application.
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "partial App must be public")]
public partial class App
{
    private readonly IRouter router;
    private readonly IValueConverter vmToViewConverter;
    private readonly IHostApplicationLifetime lifetime;
    private readonly IWindowManagerService windowManager;
    private readonly WindowBackdropService backdropService;
    private readonly WindowChromeService chromeService;

    /// <summary>
    /// Initializes a new instance of the <see cref="App"/> class.
    /// </summary>
    /// <param name="lifetime">The host application lifetime.</param>
    /// <param name="router">The application router.</param>
    /// <param name="converter">The ViewModel to View converter.</param>
    /// <param name="windowManager">The window manager service for multi-window support.</param>
    /// <param name="backdropService">The backdrop service for automatic backdrop application.</param>
    /// <param name="chromeService">The chrome service for automatic chrome application.</param>
    public App(
        IHostApplicationLifetime lifetime,
        IRouter router,
        [FromKeyedServices("VmToView")]
        IValueConverter converter,
        IWindowManagerService windowManager,
        WindowBackdropService backdropService,
        WindowChromeService chromeService)
    {
        this.lifetime = lifetime;
        this.router = router;
        this.vmToViewConverter = converter;
        this.windowManager = windowManager;
        this.backdropService = backdropService;
        this.chromeService = chromeService;
        this.InitializeComponent();
    }

    /// <summary>
    /// Invoked when the application is launched.
    /// </summary>
    /// <param name="args">Details about the launch request and process.</param>
    protected override async void OnLaunched(LaunchActivatedEventArgs args)
    {
        Current.Resources["VmToViewConverter"] = this.vmToViewConverter;

        // Exit if navigation fails
        _ = this.router.Events.OfType<NavigationError>().Subscribe(_ => this.lifetime.StopApplication());

        // Navigate to root - this creates the main window and loads MainShellViewModel,
        // then navigates to /demo which loads WindowManagerShellViewModel into the content outlet
        try
        {
            await this.router.NavigateAsync(
                "/demo",
                new FullNavigation { Target = Target.Main }).ConfigureAwait(true);
        }
        catch (NavigationFailedException ex)
        {
            Debug.WriteLine($"Failed to navigate: {ex.Message}");
            this.lifetime.StopApplication();
        }
    }
}
