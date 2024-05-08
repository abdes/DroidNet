// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor;

using System.Diagnostics;
using System.Globalization;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.DependencyInjection;
using Microsoft.UI.Xaml;
using Microsoft.Windows.AppLifecycle;
using Oxygen.Editor.ProjectBrowser.ViewModels;
using Oxygen.Editor.Services;
using Oxygen.Editor.Views;
using WinUIEx;

/// <summary>Provides application-specific behavior to supplement the default Application class.</summary>
public partial class App
{
    /// <summary>Initializes a new instance of the <see cref="App" /> class.</summary>
    public App() => this.InitializeComponent();

    public static WindowEx MainWindow { get; } = new MainWindow();

    public static UIElement? AppTitlebar { get; set; }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        base.OnLaunched(args);

        this.UnhandledException += OnAppUnhandledException;
        AppDomain.CurrentDomain.UnhandledException += OnCurrentDomainUnhandledException;

        Ioc.Default.GetRequiredService<IAppNotificationService>()
            .Initialize();

        _ = Ioc.Default.GetRequiredService<IActivationService>()
            .Where(data => data is not null and LaunchActivatedEventArgs)
            .Select(data => data as LaunchActivatedEventArgs)
            .Subscribe(
                data =>
                {
                    var navigation = Ioc.Default.GetRequiredService<INavigationService>();
                    Debug.WriteLine("Launch activation ==> navigate to project browser");
                    _ = navigation.NavigateTo(
                        typeof(StartViewModel).FullName!,
                        Ioc.Default.GetRequiredService<StartViewModel>());
                });

        /* TODO(abdes) add subscriptions for other supported activations */

        MainWindow.Content = Ioc.Default.GetRequiredService<ShellPage>();

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
        Ioc.Default.GetRequiredService<IActivationService>()
            .Activate(activationData);

        // Activate the MainWindow.
        MainWindow.Activate();
    }

    private static void OnAppUnhandledException(object sender, UnhandledExceptionEventArgs e)
        => OnUnhandledException(e.Exception, shouldShowNotification: true);

    private static void OnCurrentDomainUnhandledException(object sender, System.UnhandledExceptionEventArgs e)
        => OnUnhandledException((Exception)e.ExceptionObject, shouldShowNotification: true);

    private static void OnUnhandledException(Exception ex, bool shouldShowNotification)
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

        if (shouldShowNotification)
        {
            _ = Ioc.Default.GetService<IAppNotificationService>()
                ?.Show(
                    $"""
                     <toast>
                         <visual>
                             <binding template="ToastGeneric">
                                 <text>Unhandled Exception</text>
                                 <text>{ex.Message}</text>
                             </binding>
                         </visual>
                     </toast>
                     """);
        }

        Process.GetCurrentProcess()
            .Kill();
    }
}
