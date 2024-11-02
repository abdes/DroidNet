// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Demo;

using DroidNet.Docking.Controls;
using DroidNet.Docking.Demo.Controls;
using DroidNet.Docking.Demo.Shell;
using DroidNet.Docking.Demo.Workspace;
using DroidNet.Docking.Layouts;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DryIoc;
using Microsoft.Extensions.Logging;
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

        container.Register<MainWindow>(Reuse.Singleton);

        container.Register<ShellViewModel>(Reuse.Singleton);
        container.Register<ShellView>(Reuse.Singleton);
        container.Register<WorkspaceViewModel>(Reuse.Transient);
        container.Register<WorkspaceView>(Reuse.Transient);
        container.Register<DockableInfoView>(Reuse.Transient);
        container.Register<DockableInfoViewModel>(Reuse.Transient);
        container.Register<WelcomeView>(Reuse.Transient);
        container.Register<WelcomeViewModel>(Reuse.Transient);

        container.Register<DockPanelViewModel>(Reuse.Transient);
        container.Register<DockPanel>(Reuse.Transient);
        container.Register<IDockViewFactory, DockViewFactory>(Reuse.Singleton);
    }
}
