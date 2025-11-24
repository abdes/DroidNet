// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using System.Reactive.Linq;
using DroidNet.Aura.Decoration;
using DroidNet.Aura.Theming;
using DroidNet.Aura.Windowing;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;
using Microsoft.Windows.AppLifecycle;
using Oxygen.Editor.Services;
using Oxygen.Editor.WorldEditor.Engine;

namespace Oxygen.Editor;

/// <summary>
/// Provides application-specific behavior to supplement the default Application class.
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "Application class must always be public")]
public partial class App
{
    private readonly IRouter router;
    private readonly IValueConverter vmToViewConverter;
    private readonly IHostApplicationLifetime lifetime;
    private readonly HostingContext hostingContext;
    private readonly IActivationService activationService;
    private readonly IEngineService engineService;

    /// <summary>
    ///     Initializes a new instance of the <see cref="App"/> class.
    /// </summary>
    /// <param name="hostingContext">The hosting context for the application.</param>
    /// <param name="activationService">The activation service.</param>
    /// <param name="lifetime">The host application lifetime, used to imperatively exit the application when needed.</param>
    /// <param name="router">The application router.</param>
    /// <param name="converter">The ViewModel to View converter to be used to set the content inside the content control.</param>
    /// <param name="windowManager">The window manager service for multi-window support.</param>
    /// <param name="themeModeService">The theme mode service used to apply the requested theme to application windows.</param>
    /// <param name="backdropService">The backdrop service for automatic backdrop application.</param>
    /// <param name="chromeService">The chrome service for automatic chrome application.</param>
    /// <param name="placementService">The window placement service for restoring and saving window positions.</param>
    /// <param name="engineService">Ensures the shared engine is initialized during application startup.</param>
    /// <remarks>
    ///     In this project architecture, the single instance of the application is created by the User Interface hosted
    ///     service
    ///     as part of the application host initialization. Its lifecycle is managed together with the rest of the services.
    ///     The <paramref name="converter" /> must be available in the XAML as a static resource. Because it has dependencies
    ///     injected via the Dependency Injector, it is created in the code behind and programmatically added as a static
    ///     resource after the application is <see cref="OnLaunched" /> launched.
    /// </remarks>
    public App(
        HostingContext hostingContext,
        IActivationService activationService,
        IHostApplicationLifetime lifetime,
        IRouter router,
        [FromKeyedServices("VmToView")]
        IValueConverter converter,
        IWindowManagerService windowManager,
        IAppThemeModeService themeModeService,
        WindowBackdropService backdropService,
        WindowChromeService chromeService,
        WindowPlacementService placementService,
        IEngineService engineService)
    {
        _ = windowManager; // Unused; injected only for early initialization
        _ = backdropService; // Unused; injected only for early initialization
        _ = themeModeService; // Unused; injected only to instantiate it early
        _ = chromeService; // Unused; injected only to instantiate it early

        _ = placementService; // autonomous service

        // Create the DispatcherScheduler for the UI thread
        this.hostingContext = hostingContext;
        this.activationService = activationService;
        this.lifetime = lifetime;
        this.router = router;
        this.vmToViewConverter = converter;
        this.engineService = engineService;
        this.InitializeComponent();
    }

    /// <inheritdoc/>
    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        base.OnLaunched(args);

#if DEBUG
        this.DebugSettings.BindingFailed += (_, e) => Debug.WriteLine(e.Message);
        this.DebugSettings.XamlResourceReferenceFailed += (_, e) => Debug.WriteLine(e.Message);
#endif

        Current.Resources["VmToViewConverter"] = this.vmToViewConverter;

        this.EnsureEngineIsReady();

        this.UnhandledException += OnAppUnhandledException;
        AppDomain.CurrentDomain.UnhandledException += OnCurrentDomainUnhandledException;

        // We just want to exit if navigation fails for some reason
        _ = this.router.Events.OfType<NavigationError>().Subscribe(_ => this.lifetime.StopApplication());

        _ = this.activationService
            .ObserveOn(this.hostingContext.DispatcherScheduler)
            .OfType<LaunchActivatedEventArgs>()
            .Select(
                _ => Observable.FromAsync(
                    async () =>
                    {
                        Debug.WriteLine("Launch activation ==> navigate to project browser");
                        await this.router.NavigateAsync("/pb/home", new FullNavigation() { Target = new Target { Name = "wnd-pb" } }).ConfigureAwait(true);
                    }))
            .Concat()
            .Subscribe();

        /* TODO(abdes) add subscriptions for other supported activations */

        var activationArgs = AppInstance.GetCurrent()
            .GetActivatedEventArgs();
#pragma warning disable IDE0072 // Add missing cases

        // ReSharper disable once SwitchExpressionHandlesSomeKnownEnumValuesWithExceptionInDefault
        var activationData = activationArgs.Kind switch
        {
            // For Launch activation, the activation args don't have the data,
            // but it is provided to us in the LaunchActivatedEventArgs `args`.
            ExtendedActivationKind.Launch => args,

            // For other supported activations, just pass-through the data from
            // the activation event args.
            ExtendedActivationKind.File => activationArgs.Data,
            ExtendedActivationKind.Protocol => activationArgs.Data,

            // Any other activation kind is not supported for now
            _ => throw new NotSupportedException($"Activation kind {activationArgs.Kind} is not supported"),
        };
#pragma warning restore IDE0072 // Add missing cases

        // Activate the application.
        this.activationService.ActivateAsync(activationData).GetAwaiter().GetResult();
    }

    private void EnsureEngineIsReady()
    {
        try
        {
            // Use AsTask() to convert ValueTask to Task before blocking to avoid CA2012 warning.
            this.engineService.EnsureInitializedAsync().AsTask().GetAwaiter().GetResult();
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to initialize engine service: {ex}");
            throw;
        }
    }

    private static void OnAppUnhandledException(object sender, Microsoft.UI.Xaml.UnhandledExceptionEventArgs e)
        => OnUnhandledException(e.Exception);

    private static void OnCurrentDomainUnhandledException(object sender, System.UnhandledExceptionEventArgs e)
        => OnUnhandledException((Exception)e.ExceptionObject);

    private static void OnUnhandledException(Exception ex)
    {
        /*
         * TODO: Log and handle exceptions as appropriate.
         * https://docs.microsoft.com/windows/windows-app-sdk/api/winrt/microsoft.ui.xaml.application.unhandledexception.
         */

        var stackTrace = ex.StackTrace is not null ? $"\n--- STACKTRACE ---\n{ex.StackTrace}" : string.Empty;
        var source = ex.Source is not null ? $"\n--- SOURCE ---\n{ex.Source}" : string.Empty;
        var innerException = ex.InnerException is not null ? $"\n--- INNER ---\n{ex.InnerException}" : string.Empty;
        var message = $"""
                       --------- UNHANDLED EXCEPTION ---------
                       >>>> HRESULT: {ex.HResult.ToString(CultureInfo.InvariantCulture)}
                       --- MESSAGE ---
                       {ex.Message}{stackTrace}{source}{innerException}
                       ---------------------------------------
                       """;
        Debug.WriteLine(message);

        Process.GetCurrentProcess()
            .Kill();
    }
}
