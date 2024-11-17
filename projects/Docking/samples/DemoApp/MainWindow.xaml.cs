// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Docking.Demo.Controls;
using DroidNet.Docking.Demo.Shell;
using DroidNet.Docking.Workspace;
using DryIoc;
using Microsoft.UI.Xaml;

/* ReSharper disable PrivateFieldCanBeConvertedToLocalVariable */

namespace DroidNet.Docking.Demo;

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
public sealed partial class MainWindow
{
    private readonly Docker? docker;

    [ObservableProperty]
    private UIElement? shell;

    /// <summary>Initializes a new instance of the <see cref="MainWindow" /> class.</summary>
    /// <param name="resolver">The Dependency Injector's service provider.</param>
    public MainWindow(IResolver resolver)
    {
        this.InitializeComponent();

        // Initialize the docking tree for the demo.
        this.docker = InitializeDockingDemo(resolver);

        // Initialize the shell for this window.
        var wrapper = resolver.Resolve<Func<Docker, ShellViewModel>>();
        var shellViewModel = wrapper(this.docker);
        this.Shell = new ShellView { ViewModel = shellViewModel };

        this.Closed += (_, _) => this.docker.Dispose();
    }

    private static Docker InitializeDockingDemo(IResolver resolver)
    {
        var newDocker = new Docker();
        Anchor? anchor = null;
        try
        {
            anchor = new Anchor(AnchorPosition.Center);
            newDocker.Dock(CenterDock.New(), anchor);

            var left1 = MakeDockWithVerticalDockable(resolver, "left1");
            anchor = new AnchorLeft();
            newDocker.Dock(left1, anchor);

            var left2 = MakeDockWithVerticalDockable(resolver, "left2");
            anchor = new AnchorBottom(left1.Dockables[0]);
            newDocker.Dock(left2, anchor);
            anchor = new AnchorLeft();
            newDocker.Dock(MakeDockWithVerticalDockable(resolver, "left3"), anchor, minimized: true);
            anchor = new AnchorLeft();
            newDocker.Dock(MakeDockWithVerticalDockable(resolver, "left4"), anchor);

            anchor = new AnchorTop();
            newDocker.Dock(MakeDockWithHorizontalDockable(resolver, "top1"), anchor);

            anchor = new AnchorBottom();
            newDocker.Dock(MakeDockWithHorizontalDockable(resolver, "bottom1"), anchor, minimized: true);

            var right1 = MakeDockWithVerticalDockable(resolver, "right1");
            anchor = new AnchorRight();
            newDocker.Dock(right1, anchor);

            anchor = new Anchor(AnchorPosition.Right, right1.Dockables[0]);
            newDocker.Dock(MakeDockWithVerticalDockable(resolver, "right2"), anchor);

            anchor = new Anchor(AnchorPosition.Bottom, right1.Dockables[0]);
            newDocker.Dock(MakeDockWithVerticalDockable(resolver, "right3"), anchor);

            anchor = null; // Dispose ownership all transferred
        }
        finally
        {
            anchor?.Dispose();
        }

        newDocker.DumpWorkspace();

        return newDocker;
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
