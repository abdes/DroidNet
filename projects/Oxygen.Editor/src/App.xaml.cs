// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using System.Reactive.Linq;
using System.Runtime.ExceptionServices;
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
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Services;

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
        AppDomain.CurrentDomain.FirstChanceException += OnFirstChanceException;

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

    private static void OnAppUnhandledException(object sender, Microsoft.UI.Xaml.UnhandledExceptionEventArgs e)
        => OnUnhandledException(e.Exception);

    private static void OnCurrentDomainUnhandledException(object sender, System.UnhandledExceptionEventArgs e)
        => OnUnhandledException((Exception)e.ExceptionObject);

    private static void OnFirstChanceException(object? sender, FirstChanceExceptionEventArgs e)
    {
        var parts = new List<string>(16);
        try
        {
            var ex = e.Exception;
            if (!IsRelevantFirstChance(ex))
            {
                return;
            }

            // If we reached here, log a diagnostic dump similar to the previous
            // implementation for E_INVALIDARG but for a broader set of cases.
            parts.Add("--------- FIRST-CHANCE (DIAGNOSTIC) ---------\n");
            parts.Add(string.Create(CultureInfo.InvariantCulture, $"Timestamp: {DateTimeOffset.UtcNow:O} (UTC)\n"));
            parts.Add(string.Create(CultureInfo.InvariantCulture, $"ThreadId: {Environment.CurrentManagedThreadId}\n"));
            parts.Add(string.Create(CultureInfo.InvariantCulture, $"Type: {ex.GetType().FullName}\n"));
            parts.Add(string.Create(CultureInfo.InvariantCulture, $"HRESULT: {ex.HResult}\n"));
            parts.Add("--- MESSAGE ---\n");
            parts.Add(ex.Message + "\n");

            // stacktrace or full ToString() fallback
            if (!string.IsNullOrWhiteSpace(ex.StackTrace))
            {
                parts.Add("--- STACKTRACE ---\n");
                parts.Add(ex.StackTrace + "\n");
            }

            DumpInner(parts, ex.InnerException, 6, 1);
            parts.Add("--------------------------------------------\n");

            var message = CombineParts(parts);
            Debug.WriteLine(message);
        }
#pragma warning disable CA1031 // Do not catch general exception types
        catch (Exception logEx)
        {
            // never let logging of first-chance exceptions crash the process
            Debug.WriteLine(string.Create(CultureInfo.InvariantCulture, $"Failed to log first-chance exception: {logEx}\n"));
        }
#pragma warning restore CA1031 // Do not catch general exception types

        // local function to append inner exception chain to parts
        static void DumpInner(List<string> parts, Exception? inner, int remaining, int index)
        {
            if (inner is null || remaining <= 0)
            {
                return;
            }

            parts.Add(string.Create(CultureInfo.InvariantCulture, $"--- INNER ({index}) ---\n"));
            parts.Add(string.Create(CultureInfo.InvariantCulture, $"Type: {inner.GetType().FullName}\n"));
            parts.Add(inner.Message + "\n");
            if (!string.IsNullOrWhiteSpace(inner.StackTrace))
            {
                parts.Add(inner.StackTrace + "\n");
            }

            DumpInner(parts, inner.InnerException, remaining - 1, index + 1);
        }

        static string CombineParts(List<string> parts)
        {
            // compute total length and assemble with single allocation
            var total = 0;
            foreach (var p in parts)
            {
                total += p?.Length ?? 0;
            }

            var message = string.Create(total, parts, (span, state) =>
            {
                var pos = 0;
                foreach (var s in state)
                {
                    if (string.IsNullOrEmpty(s))
                    {
                        continue;
                    }

                    var src = s.AsSpan();
                    src.CopyTo(span.Slice(pos, src.Length));
                    pos += src.Length;
                }
            });
            return message;
        }
    }

    // Local predicate that decides whether this first-chance exception
    // is one of the commonly-observed, usually harmless WinUI / WinAppSDK
    // errors we want to log and inspect.
    private static bool IsRelevantFirstChance(Exception ex)
    {
        if (ex is null)
        {
            return false;
        }

        // Common HRESULTs observed as first-chance in WinRT/WinUI interop
        // and native calls. These are frequently thrown and caught internally
        // but are useful to capture during development for diagnostics.
        // Values expressed with unchecked cast for portability.
        var knownHResults = new HashSet<int>
        {
            unchecked((int)0x80070057), // E_INVALIDARG
            unchecked((int)0x80004003), // E_POINTER
            unchecked((int)0x8000000B), // E_BOUNDS
            unchecked((int)0x80070005), // E_ACCESSDENIED
            unchecked((int)0x80004004), // E_ABORT
            unchecked((int)0x8001010E), // RPC_E_WRONG_THREAD
            unchecked((int)0x800401F0), // CO_E_NOTINITIALIZED
        };

        if (knownHResults.Contains(ex.HResult))
        {
            return true;
        }

        // Heuristics based on exception message for cases where HResult
        // is not specific or not set. Match typical substrings seen in
        // parameter/interop/bounds/pointer errors.
        var msg = ex.Message;
#pragma warning disable IDE0046 // Convert to conditional expression
        if (string.IsNullOrEmpty(msg))
        {
            return false;
        }
#pragma warning restore IDE0046 // Convert to conditional expression

        return msg.Contains("parameter", StringComparison.OrdinalIgnoreCase)
            || msg.Contains("expected range", StringComparison.OrdinalIgnoreCase)
            || msg.Contains("bounds", StringComparison.OrdinalIgnoreCase)
            || msg.Contains("pointer", StringComparison.OrdinalIgnoreCase)
            || msg.Contains("access is denied", StringComparison.OrdinalIgnoreCase)
            || msg.Contains("class not registered", StringComparison.OrdinalIgnoreCase)
            || msg.Contains("no such interface supported", StringComparison.OrdinalIgnoreCase)
            || msg.Contains("wrong thread", StringComparison.OrdinalIgnoreCase);
    }

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
                       {ex}{stackTrace}{source}{innerException}
                       ---------------------------------------
                       """;
        Debug.WriteLine(message);

        Process.GetCurrentProcess()
            .Kill();
    }

    private void EnsureEngineIsReady()
    {
        // FIXME(abdes): this engine initialization should be done better with UI feedback and cancellation support.
        try
        {
            // Use AsTask() to convert ValueTask to Task before blocking to avoid CA2012 warning.
            this.engineService.InitializeAsync().AsTask().GetAwaiter().GetResult();
            this.engineService.StartAsync().AsTask().GetAwaiter().GetResult();
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to initialize engine service: {ex}");
            throw;
        }
    }
}
