// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Docking.Demo.Controls;
using DroidNet.Docking.Layouts.GridFlow;
using DroidNet.Docking.Workspace;
using DryIoc;
using Microsoft.UI.Xaml;

namespace DroidNet.Docking.Demo.Workspace;

/// <summary>
/// The ViewModel for the docking workspace.
/// </summary>
/// <remarks>
/// <para>
/// The <see cref="WorkspaceViewModel"/> class is responsible for managing the state and behavior of the docking workspace.
/// It initializes the workspace content and updates it based on layout changes.
/// </para>
/// <para>
/// This ViewModel uses dependency injection to resolve required services and view models, ensuring a decoupled and testable design.
/// </para>
/// </remarks>
/// <example>
/// <para>
/// To create an instance of <see cref="WorkspaceViewModel"/> and bind it to a view, use the following code:
/// </para>
/// <code><![CDATA[
/// var docker = new CustomDocker();
/// var layout = new GridFlowLayout(new CustomDockViewFactory());
/// var workspaceViewModel = new WorkspaceViewModel(docker, layout);
/// ]]></code>
/// </example>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "ViewModel must be public because the 'ViewModel' property in the View is public")]
public sealed partial class WorkspaceViewModel : ObservableObject, IDisposable
{
    private readonly Docker docker;
    private bool isDisposed;

    /// <summary>
    /// Gets or sets the workspace content.
    /// </summary>
    /// <value>
    /// The <see cref="UIElement"/> representing the workspace content.
    /// </value>
    [ObservableProperty]
    private UIElement? workspaceContent;

    /// <summary>
    /// Initializes a new instance of the <see cref="WorkspaceViewModel"/> class.
    /// </summary>
    /// <param name="resolver">The resolver used to resolve required services and view models.</param>
    public WorkspaceViewModel(IResolver resolver)
    {
        var dockViewFactory = new DockViewFactory(resolver);
        var layout = new GridFlowLayout(dockViewFactory);
        this.docker = InitializeDockingDemo(resolver);
        this.UpdateContent(this.docker, layout);

        this.docker.LayoutChanged += (_, args) =>
        {
            if (args.Reason is LayoutChangeReason.Docking)
            {
                this.UpdateContent(this.docker, layout);
            }
        };
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        this.docker.Dispose();

        this.isDisposed = true;
    }

    private static Docker InitializeDockingDemo(IResolver resolver)
    {
        var newDocker = new Docker();
        Anchor? anchor = null;
        try
        {
            anchor = new Anchor(AnchorPosition.Center);
            newDocker.Dock(CenterDock.New(), anchor);

            var left1 = MakeDockWithVerticalDockable(resolver, "A long left1");
            anchor = new AnchorLeft();
            newDocker.Dock(left1, anchor);

            var left2 = MakeDockWithVerticalDockable(resolver, "Very long title for left 2");
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

    /// <summary>
    /// Updates the workspace content based on the current layout.
    /// </summary>
    /// <param name="docker">The docker instance used to manage the docking operations within the workspace.</param>
    /// <param name="layout">The layout engine used to arrange the dockable entities within the workspace.</param>
    /// <exception cref="InvalidOperationException">
    /// Thrown when the provided layout engine does not produce a <see cref="UIElement"/>.
    /// </exception>
    private void UpdateContent(Docker docker, GridFlowLayout layout)
    {
        docker.Layout(layout);
        var content = layout.CurrentGrid;
        this.WorkspaceContent = content as UIElement ??
                                throw new InvalidOperationException(
                                    "the provided layout engine does not produce a UIElement");
    }
}
