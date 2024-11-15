// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Samples.Simple;

using DroidNet.Routing.Samples.Simple.Navigation;
using DroidNet.Routing.Samples.Simple.Shell;
using DryIoc;
using Microsoft.Extensions.Logging;
using Serilog;
using Serilog.Extensions.Logging;
using Window = Microsoft.UI.Xaml.Window;

public static class HostingExtensions
{
    public static void ConfigureLogging(this IContainer container)
    {
        container.RegisterInstance<ILoggerFactory>(new SerilogLoggerFactory(Log.Logger));

        container.Register(
            Made.Of(
                _ => ServiceInfo.Of<ILoggerFactory>(),
                f => f.CreateLogger(null!)),
            setup: Setup.With(condition: r => r.Parent.ImplementationType == null));

        container.Register(
            Made.Of(
                _ => ServiceInfo.Of<ILoggerFactory>(),
                f => f.CreateLogger(Arg.Index<Type>(0)),
                r => r.Parent.ImplementationType),
            setup: Setup.With(condition: r => r.Parent.ImplementationType != null));
    }

    public static void ConfigureApplicationServices(this IContainer container)
    {
        // The Main Window is a singleton and its content can be re-assigned as needed. It is registered with a key that
        // corresponding to name of the special target <see cref="Target.Main" />.
        container.Register<Window, MainWindow>(Reuse.Singleton, serviceKey: Target.Main);

        // Views and ViewModels
        container.Register<ShellView>(Reuse.Singleton);
        container.Register<ShellViewModel>(Reuse.Singleton);
        container.Register<PageOneView>(Reuse.Singleton);
        container.Register<PageOneViewModel>(Reuse.Singleton);
        container.Register<PageTwoView>(Reuse.Singleton);
        container.Register<PageTwoViewModel>(Reuse.Singleton);
        container.Register<PageThreeView>(Reuse.Singleton);
        container.Register<PageThreeViewModel>(Reuse.Singleton);
        container.Register<RoutedNavigationView>(Reuse.Singleton);
        container.Register<RoutedNavigationViewModel>(Reuse.Singleton);
        container.Register<SettingsView>(Reuse.Singleton);
        container.Register<SettingsViewModel>(Reuse.Singleton);
    }
}
