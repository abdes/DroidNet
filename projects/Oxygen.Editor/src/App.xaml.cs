// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using System.Reactive.Linq;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;
using Microsoft.Windows.AppLifecycle;
using Oxygen.Editor.Services;

namespace Oxygen.Editor;

/// <summary>
/// Provides application-specific behavior to supplement the default Application class.
/// </summary>
public partial class App
{
    private readonly IRouter router;
    private readonly IValueConverter vmToViewConverter;
    private readonly IHostApplicationLifetime lifetime;
    private readonly HostingContext hostingContext;
    private readonly IActivationService activationService;

    /// <summary>
    /// Initializes a new instance of the <see cref="App"/> class.
    /// </summary>
    /// <param name="hostingContext">The hosting context for the application.</param>
    /// <param name="activationService">The activation service.</param>
    /// <param name="lifetime">The host application lifetime, used to imperatively exit the application when needed.</param>
    /// <param name="router">The application router.</param>
    /// <param name="converter">The ViewModel to View converter to be used to set the content inside the content control.</param>
    /// <remarks>
    /// The <paramref name="converter"/> needs to be available in the XAML as a static resource. However, because it has
    /// dependencies injected via the Dependency Injector, we create it in the code behind and programmatically add it as a static
    /// resource after the application is <see cref="OnLaunched">launched</see>.
    /// </remarks>
    public App(
        HostingContext hostingContext,
        IActivationService activationService,
        IHostApplicationLifetime lifetime,
        IRouter router,
        [FromKeyedServices("VmToView")]
        IValueConverter converter)
    {
        // Create the DispatcherScheduler for the UI thread
        this.hostingContext = hostingContext;
        this.activationService = activationService;
        this.lifetime = lifetime;
        this.router = router;
        this.vmToViewConverter = converter;
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
                        await this.router.NavigateAsync("/pb/home", new FullNavigation() { Target = Target.Main }).ConfigureAwait(true);
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
