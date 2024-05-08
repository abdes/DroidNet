// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT).
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor;

using System.Diagnostics;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.DependencyInjection;
using Microsoft.UI.Xaml;
using Microsoft.Windows.AppLifecycle;
using Oxygen.Editor.ProjectBrowser.ViewModels;
using Oxygen.Editor.Services;
using Oxygen.Editor.Views;
using WinUIEx;

/// <summary>The application class.</summary>
/// Learn more about WinUI 3
/// <see href="https://docs.microsoft.com/windows/apps/winui/winui3/" />
/// .
public partial class App
{
    /// <summary>Initializes a new instance of the <see cref="App" /> class.</summary>
    public App() => this.InitializeComponent();

    public static WindowEx MainWindow { get; } = new MainWindow();

    public static UIElement? AppTitlebar { get; set; }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        base.OnLaunched(args);

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

        // TODO(abdes) temporary for testing
        /*
        GetService<IAppNotificationService>().Show(
            string.Format(
                CultureInfo.InvariantCulture,
                "AppNotificationSamplePayload".GetLocalized(),
                AppContext.BaseDirectory));
        */

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
}
