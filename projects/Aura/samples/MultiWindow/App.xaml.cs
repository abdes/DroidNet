// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
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
public partial class App
{
    private readonly IRouter router;
    private readonly IValueConverter vmToViewConverter;
    private readonly IHostApplicationLifetime lifetime;
    private readonly IWindowManagerService windowManager;

    /// <summary>
    /// Initializes a new instance of the <see cref="App"/> class.
    /// </summary>
    /// <param name="lifetime">The host application lifetime.</param>
    /// <param name="router">The application router.</param>
    /// <param name="converter">The ViewModel to View converter.</param>
    /// <param name="windowManager">The window manager service for multi-window support.</param>
    public App(
        IHostApplicationLifetime lifetime,
        IRouter router,
        [FromKeyedServices("VmToView")]
        IValueConverter converter,
        IWindowManagerService windowManager)
    {
        this.lifetime = lifetime;
        this.router = router;
        this.vmToViewConverter = converter;
        this.windowManager = windowManager;
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

        // Subscribe to window lifecycle events for logging and demo purposes
        _ = this.windowManager.WindowEvents.Subscribe(evt =>
        {
            System.Diagnostics.Debug.WriteLine(
                $"Window Event: {evt.EventType} - {evt.Context.Title} ({evt.Context.Id})");
        });

        // Navigate to the demo view - this will create the main window via routing
        try
        {
            await this.router.NavigateAsync(
                "/demo",
                new FullNavigation { Target = Target.Main }).ConfigureAwait(true);
        }
        catch (NavigationFailedException ex)
        {
            System.Diagnostics.Debug.WriteLine($"Failed to navigate: {ex.Message}");
            this.lifetime.StopApplication();
        }
    }
}
