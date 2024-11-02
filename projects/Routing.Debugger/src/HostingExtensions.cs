// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger;

using DroidNet.Docking.Controls;
using DroidNet.Docking.Layouts;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing.Debugger.UI.Config;
using DroidNet.Routing.Debugger.UI.Docks;
using DroidNet.Routing.Debugger.UI.Shell;
using DroidNet.Routing.Debugger.UI.State;
using DroidNet.Routing.Debugger.UI.UrlTree;
using DroidNet.Routing.Debugger.UI.WorkSpace;
using DroidNet.Routing.Debugger.Welcome;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;
using Serilog;
using Serilog.Extensions.Logging;

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
        // Set up the view model to view converters. We're using the standard
        // converter, and a custom one with fall back if the view cannot be
        // located.
        container.Register<IViewLocator, DefaultViewLocator>(Reuse.Singleton);
        container.Register<IValueConverter, ViewModelToView>(Reuse.Singleton, serviceKey: "VmToView");

        container.Register<DockViewFactory>(Reuse.Singleton);
        container.Register<DockPanelViewModel>(Reuse.Transient);
        container.Register<DockPanel>(Reuse.Transient);

        /*
         * Configure the Application's Windows. Each window represents a target in which to open the requested url. The
         * target name is the key used when registering the window type.
         *
         * There should always be a Window registered for the special target <c>_main</c>.
         */

        // The Main Window is a singleton and its content can be re-assigned as needed. It is registered with a key that
        // corresponding to name of the special target <see cref="Target.Main" />.
        container.Register<Window, MainWindow>(Reuse.Singleton, serviceKey: Target.Main);

        container.Register<ShellViewModel>(Reuse.Singleton);
        container.Register<ShellView>(Reuse.Singleton);
        container.Register<EmbeddedAppViewModel>(Reuse.Transient);
        container.Register<EmbeddedAppView>(Reuse.Transient);
        container.Register<WelcomeViewModel>(Reuse.Transient);
        container.Register<WelcomeView>(Reuse.Transient);
        container.Register<WorkSpaceViewModel>(Reuse.Transient);
        container.Register<WorkSpaceView>(Reuse.Transient);
        container.Register<RoutesViewModel>(Reuse.Singleton);
        container.Register<RoutesView>(Reuse.Transient);
        container.Register<UrlTreeViewModel>(Reuse.Singleton);
        container.Register<UrlTreeView>(Reuse.Transient);
        container.Register<RouterStateViewModel>(Reuse.Singleton);
        container.Register<RouterStateView>(Reuse.Transient);
        container.Register<IDockViewFactory, DockViewFactory>(Reuse.Singleton);
    }
}
