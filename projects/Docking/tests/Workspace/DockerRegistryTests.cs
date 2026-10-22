// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Docking.Detail;
using DroidNet.Docking.Workspace;

namespace DroidNet.Docking.Tests.Workspace;

/// <summary>Unit test cases for the docks and dockables a <see cref="Docker" /> creates, and its docking maneuver.</summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(Docker)}.Registry")]
public sealed class DockerRegistryTests : IDisposable
{
    private const string DockableId = "dockable1";

    private readonly Docker sut = new();

    [TestCleanup]
    public void Dispose() => this.sut.Dispose();

    [TestMethod]
    public void CreateDockable_GivenNewId_IsFoundAndListed()
    {
        var dockable = this.sut.CreateDockable<Dockable>(DockableId);

        _ = this.sut.FindDockable(DockableId).Should().BeSameAs(dockable);
        _ = this.sut.Dockables.Should().ContainSingle().Which.Should().BeSameAs(dockable);
    }

    [TestMethod]
    public void CreateDockable_GivenUsedId_Throws()
    {
        _ = this.sut.CreateDockable<Dockable>(DockableId);

        var act = () => this.sut.CreateDockable<Dockable>(DockableId);

        _ = act.Should()
            .Throw<InvalidOperationException>()
            .WithMessage($"attempt to create a dockable with an already used ID: {DockableId}");
    }

    [TestMethod]
    public void CreateDockable_SameIdInAnotherDocker_IsIndependent()
    {
        using var other = new Docker();
        var mine = this.sut.CreateDockable<Dockable>(DockableId);

        var theirs = other.CreateDockable<Dockable>(DockableId);

        _ = theirs.Should().NotBeSameAs(mine);
        _ = this.sut.FindDockable(DockableId).Should().BeSameAs(mine);
        _ = other.FindDockable(DockableId).Should().BeSameAs(theirs);
    }

    [TestMethod]
    public void CreateDockable_WithArguments_PassesThemAfterTheId()
    {
        var dockable = this.sut.CreateDockable<CustomDockable>(DockableId, 100, "arg-value");

        _ = dockable.Id.Should().Be(DockableId);
        _ = dockable.IntArg.Should().Be(100);
        _ = dockable.StrArg.Should().Be("arg-value");
    }

    [TestMethod]
    public void CreateDockable_GivenBadArguments_Throws()
    {
        var act = () => this.sut.CreateDockable<Dockable>(DockableId, 1, 2, 3);

        _ = act.Should().Throw<ObjectCreationException>();
        _ = this.sut.FindDockable(DockableId).Should().BeNull();
    }

    [TestMethod]
    public void FindDockable_GivenUnknownId_ReturnsNull()
        => _ = this.sut.FindDockable("__").Should().BeNull();

    [TestMethod]
    public void DisposedDockable_IsForgottenAndItsIdReusable()
    {
        var dockable = this.sut.CreateDockable<Dockable>(DockableId);

        dockable.Dispose();

        _ = this.sut.FindDockable(DockableId).Should().BeNull();
        _ = this.sut.Dockables.Should().BeEmpty();
        _ = this.sut.CreateDockable<Dockable>(DockableId).Should().NotBeSameAs(dockable);
    }

    [TestMethod]
    public void Dispose_DisposesTheDockablesNoDockOwns()
    {
        var docker = new Docker();
        var dockable = docker.CreateDockable<Dockable>(DockableId);
        var disposed = false;
        dockable.OnDisposed += (_, _) => disposed = true;

        docker.Dispose();

        _ = disposed.Should().BeTrue();
        _ = docker.Dockables.Should().BeEmpty();
    }

    [TestMethod]
    public void CreateDock_GivesIdsUniqueInTheDocker()
    {
        using var first = this.sut.CreateDock<ToolDock>();
        using var second = this.sut.CreateDock<ToolDock>();

        _ = second.Id.Should().NotBe(first.Id);
    }

    [TestMethod]
    public void CreateDock_WithArguments_PassesThem()
    {
        using var dock = this.sut.CreateDock<CustomDock>(100, "arg-value");

        _ = dock.IntArg.Should().Be(100);
        _ = dock.StrArg.Should().Be("arg-value");
    }

    [TestMethod]
    public void CreateDock_GivenBadArguments_Throws()
    {
        var act = () => this.sut.CreateDock<CustomDock>(1, 2, 3);

        _ = act.Should().Throw<ObjectCreationException>();
    }

    [TestMethod]
    public void DockingMode_IsRaisedOnlyByItsDocker()
    {
        using var other = new Docker();
        using var dock = this.sut.CreateDock<ToolDock>();
        var mine = new List<IDock?>();
        var theirs = new List<IDock?>();
        this.sut.DockingModeChanged += (_, args) => mine.Add(args.DockBeingDocked);
        other.DockingModeChanged += (_, args) => theirs.Add(args.DockBeingDocked);

        this.sut.EnterDockingMode(dock);
        this.sut.LeaveDockingMode();

        _ = mine.Should().Equal(dock, null);
        _ = theirs.Should().BeEmpty();
    }

    [TestMethod]
    public void LeaveDockingMode_WithoutManeuver_RaisesNothing()
    {
        var raised = false;
        this.sut.DockingModeChanged += (_, _) => raised = true;

        this.sut.LeaveDockingMode();

        _ = raised.Should().BeFalse();
    }

    private sealed partial class CustomDockable(string id, int intArg, string strArg) : Dockable(id)
    {
        public int IntArg => intArg;

        public string StrArg => strArg;
    }

    private sealed partial class CustomDock(int intArg, string strArg) : Dock
    {
        public int IntArg => intArg;

        public string StrArg => strArg;
    }
}
