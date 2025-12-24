// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using DroidNet.Aura.Dialogs;
using DroidNet.Aura.Theming;
using DroidNet.Aura.Windowing;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Samples.WinPackagedApp;

/// <summary>
///     Provides application-specific behavior to supplement the default <see cref="Application" /> class.
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "never for partial Application class")]
public partial class App
{
    private readonly IRouter router;
    private readonly IValueConverter vmToViewConverter;
    private readonly IHostApplicationLifetime lifetime;
    private readonly IWindowManagerService windowManager;
    private readonly IDialogService dialogService;

    /// <summary>
    ///     Initializes a new instance of the <see cref="App" /> class.
    /// </summary>
    /// <param name="lifetime">The host application lifetime, used to imperatively exit the application when needed.</param>
    /// <param name="router">The application router.</param>
    /// <param name="converter">The ViewModel to View converter used to set the content inside the content control.</param>
    /// <param name="windowManager">Window manager service - injected to force early initialization before navigation.</param>
    /// <param name="dialogService">Dialog service used to show dialogs without exposing XamlRoot.</param>
    /// <param name="themeModeService">The theme mode service used to apply the requested theme to application windows.</param>
    /// <remarks>
    ///     In this project architecture, the single instance of the application is created by the User Interface hosted
    ///     service
    ///     as part of the application host initialization. Its lifecycle is managed together with the rest of the services.
    ///     The <paramref name="converter" /> must be available in the XAML as a static resource. Because it has dependencies
    ///     injected via the Dependency Injector, it is created in the code behind and programmatically added as a static
    ///     resource after the application is <see cref="OnLaunched" /> launched.
    /// </remarks>
    public App(
        IHostApplicationLifetime lifetime,
        IRouter router,
        [FromKeyedServices("VmToView")]
        IValueConverter converter,
        IWindowManagerService windowManager,
        IDialogService dialogService,
        IAppThemeModeService themeModeService)
    {
        // Force WindowManagerService initialization BEFORE navigation starts
        // This ensures it subscribes to router events before the main window is created
        _ = themeModeService;

        this.lifetime = lifetime;
        this.router = router;
        this.vmToViewConverter = converter;
        this.windowManager = windowManager;
        this.dialogService = dialogService;

        // Subscribe to window closing events to request confirmation
        this.windowManager.WindowClosing += this.OnWindowClosingAsync;

        this.InitializeComponent();
    }

    /// <summary>
    ///     Invoked when the application is launched.
    /// </summary>
    /// <param name="args">Details about the launch request and process.</param>
    protected override async void OnLaunched(LaunchActivatedEventArgs args)
    {
        Current.Resources["VmToViewConverter"] = this.vmToViewConverter;

        // Exit if navigation fails
        _ = this.router.Events.OfType<NavigationError>().Subscribe(_ => this.lifetime.StopApplication());

        try
        {
            await this.router.NavigateAsync("/", new FullNavigation { Target = Target.Main }).ConfigureAwait(true);
        }
        catch (NavigationFailedException)
        {
            this.lifetime.StopApplication();
        }
    }

    private async System.Threading.Tasks.Task OnWindowClosingAsync(object? sender, WindowClosingEventArgs e)
    {
        var confirmed = await this.dialogService
            .ConfirmAsync(
                "Confirm Close",
                "Are you sure you want to close this window?")
            .ConfigureAwait(true);

        if (!confirmed)
        {
            e.Cancel = true;
        }
    }
}
