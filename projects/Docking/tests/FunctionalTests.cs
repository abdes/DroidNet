// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Detail;
using Microsoft.VisualStudio.TestTools.UnitTesting;

[TestClass]
[ExcludeFromCodeCoverage]
public class FunctionalTests
{
    /*
     [TestMethod]
       public void TypicalUsage()
       {
           var factory = new TestDockingFactory();
           var docker = factory.GetDocker();
           var dockable = factory.CreateDockable("my_view", new object());
           var dock = factory.CreateDock();

           _ = dock.IsEmpty.Should().BeTrue();

           docker.AddDockable(dockable, dock, DockingState.Minimized);

           _ = docker.Dockables.Should().Contain(dockable);
           _ = dock.Dockables.Should().Contain(dockable);
           _ = dockable.DockingState.Should().Be(DockingState.Minimized);
           _ = dock.ShouldPresent.Should().BeFalse();

           docker.PinDockable(dockable);
           _ = dockable.DockingState.Should().Be(DockingState.Pinned);
           _ = dock.ShouldPresent.Should().BeTrue();

           dock.ActiveDockable = dockable;
           _ = dock.ActiveDockable.Should().Be(dockable);
           _ = dock.IsActive.Should().BeTrue();

           docker.MinimizeDockable(dockable);
           _ = dockable.DockingState.Should().Be(DockingState.Minimized);
           _ = dock.ActiveDockable.Should().BeNull();
           _ = dock.IsActive.Should().BeFalse();
           _ = dock.ShouldPresent.Should().BeFalse();

           docker.CloseDockable(dockable);
           _ = docker.Dockables.Should().BeEmpty();
           _ = dock.Dockables.Should().BeEmpty();
           _ = dock.IsEmpty.Should().BeTrue();
           _ = dockable.DockingState.Should().Be(DockingState.Undocked);
       }
   */

    [TestMethod]
    public void NewTypicalUsage()
    {
        var docker = new Docker();

        var root = CreateToolDockOrFail();
        docker.DockToRoot(root);
        DumpGroup(docker.Root);

        var dock = CreateToolDockOrFail();
        docker.DockToRoot(dock, AnchorPosition.Left);
        DumpGroup(docker.Root);

        docker.Dock(CreateToolDockOrFail(), new AnchorBottom(dock.Id));
        docker.Dock(CreateToolDockOrFail(), new AnchorBottom(dock.Id));
        DumpGroup(docker.Root);

        // TODO: if dock is moved, update its old parent group
        docker.DockToRoot(CreateToolDockOrFail(), AnchorPosition.Top);
        docker.DockToRoot(CreateToolDockOrFail(), AnchorPosition.Right);
        docker.DockToRoot(CreateToolDockOrFail(), AnchorPosition.Bottom);
        DumpGroup(docker.Root);
    }

    private static ToolDock CreateToolDockOrFail()
        => ToolDock.New() ?? throw new AssertFailedException("could not create dock");

    private static void DumpGroup(IDockGroup group, string indent = "")
    {
        Debug.WriteLine($"{indent}{group}");
        if (group.First is not null)
        {
            DumpGroup(group.First, indent + "   ");
        }

        if (group.Second is not null)
        {
            DumpGroup(group.Second, indent + "   ");
        }
    }
}

/*
public class TestDockingFactory : DockingFactoryBase
{
    public override IDocker GetDocker() => new Docker() { Position = DockingArea.Left };
}
*/
