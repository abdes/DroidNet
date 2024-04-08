// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

/* ReSharper disable PrivateFieldCanBeConvertedToLocalVariable */

namespace DroidNet.Docking.Demo;

using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Docking.Demo.Controls;
using DroidNet.Docking.Demo.Shell;
using DroidNet.Docking.Detail;
using DroidNet.Docking.Utils;
using DroidNet.Hosting.Generators;
using DryIoc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml;

/// <summary>The User Interface's main window.</summary>
/// <remarks>
/// <para>
/// This window is created and activated when the Application is Launched. This is preferred to the alternative of doing that in
/// the hosted service to keep the control of window creation and destruction under the application itself. Not all applications
/// have a single window, and it is often not obvious which window is considered the main window, which is important in
/// determining when the UI lifetime ends.
/// </para>
/// <para>
/// The window does not have a view model and does not need one. The design principle is that windows are here only to do window
/// stuff and the content inside the window is provided by a 'shell' view that will in turn load the appropriate content based on
/// the application active route or state.
/// </para>
/// </remarks>
[ExcludeFromCodeCoverage]
[ObservableObject]
[InjectAs(ServiceLifetime.Singleton)]
public sealed partial class MainWindow
{
    [ObservableProperty]
    private UIElement? shell;

    /// <summary>Initializes a new instance of the <see cref="MainWindow" /> class.</summary>
    /// <param name="resolver">The Dependency Injector's service provider.</param>
    public MainWindow(IResolver resolver)
    {
        this.InitializeComponent();

        // Initialize the docking tree for the demo.
        var docker = InitializeDockingDemo(resolver);

        // Initialize the shell for this window.
        var wrapper = resolver.Resolve<Func<Docker, ShellViewModel>>();
        var shellViewModel = wrapper(docker);
        this.Shell = new ShellView { ViewModel = shellViewModel };
    }

    private static Docker InitializeDockingDemo(IResolver resolver)
    {
        var docker = new Docker();
        docker.DockToCenter(CenterDock.New());

        var left1 = MakeDockWithVerticalDockable(resolver, "left1");
        docker.DockToRoot(left1, AnchorPosition.Left);

        var left2 = MakeDockWithVerticalDockable(resolver, "left2");
        docker.Dock(left2, new AnchorBottom(left1.Dockables[0]));

        docker.DockToRoot(MakeDockWithVerticalDockable(resolver, "left3"), AnchorPosition.Left, minimized: true);
        docker.Root.DumpGroup();
        docker.DockToRoot(MakeDockWithVerticalDockable(resolver, "left4"), AnchorPosition.Left);
        docker.Root.DumpGroup();
        docker.DockToRoot(MakeDockWithHorizontalDockable(resolver, "top1"), AnchorPosition.Top);
        docker.DockToRoot(MakeDockWithHorizontalDockable(resolver, "bottom1"), AnchorPosition.Bottom, minimized: true);

        var right1 = MakeDockWithVerticalDockable(resolver, "right1");
        docker.DockToRoot(right1, AnchorPosition.Right);
        docker.Dock(
            MakeDockWithVerticalDockable(resolver, "right2"),
            new Anchor(AnchorPosition.Right, right1.Dockables[0]));
        docker.Dock(
            MakeDockWithVerticalDockable(resolver, "right3"),
            new Anchor(AnchorPosition.Bottom, right1.Dockables[0]));

        docker.Root.DumpGroup();

        return docker;
    }

    private static ToolDock MakeDockWithVerticalDockable(IResolver resolver, string dockableId)
    {
        var dock = ToolDock.New();
        var dockable = Dockable.New(dockableId);
        dockable.ViewModel = resolver.Resolve<Func<IDockable, DockableInfoViewModel>>()(dockable);
        dockable.PreferredWidth = new Width(300);
        dock.AdoptDockable(dockable);
        return dock;
    }

    private static ToolDock MakeDockWithHorizontalDockable(IResolver resolver, string dockableId)
    {
        var dock = ToolDock.New();
        var dockable = Dockable.New(dockableId);
        dockable.ViewModel = resolver.Resolve<Func<IDockable, DockableInfoViewModel>>()(dockable);
        dockable.PreferredHeight = new Height(200);
        dock.AdoptDockable(dockable);
        return dock;
    }
}
