// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Mocks;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(RootDockGroup)}")]
public class RootDockGroupTests : VerifyBase, IDisposable
{
    private readonly RootDockGroup sut = new();

    [TestCleanup]
    public void Dispose()
    {
        this.sut.Dispose();
        GC.SuppressFinalize(this);
    }

    [TestMethod]
    public void RootDockGroup_ShouldHaveCenter_WhenCreated() =>
        _ = this.sut.First?.IsCenter.Should().BeTrue();

    [TestMethod]
    public Task DockCenter_ShouldAddDockToCenter_WhenCenterIsEmpty()
    {
        var act = () =>
        {
            // Arrange
            using var dock = new SimpleDock();

            // Act
            this.sut.DockCenter(dock);
        };

        // Assert
        _ = act.Should().NotThrow();
        _ = this.sut.First?.IsCenter.Should().BeTrue();
        _ = this.sut.First?.Docks.Should().HaveCount(1);
        return this.Verify(this.sut).UseDirectory("Snapshots");
    }

    [TestMethod]
    public void DockCenter_ShouldThrow_WhenCenterIsNotEmpty()
    {
        var act = () =>
        {
            // Arrange
            using var dock = new SimpleDock();
            this.sut.DockCenter(dock);
            using var anotherDock = new SimpleDock();

            // Act
            this.sut.DockCenter(anotherDock);
        };

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void DockLeft_ShouldAddBeforeCenter()
    {
        // Setup
        using var dock = new SimpleDock();

        // Act
        this.sut.DockLeft(dock);

        // Assert
        var left = this.sut.First;
        _ = left.Should().NotBeNull("adding to left for the first time should create a left group under the root");

        var center = this.sut.Second;
        _ = center?.IsCenter.Should().BeTrue("the left group always comes before the center");
    }

    [TestMethod]
    public void DockLeft_ShouldAddLeftTray()
    {
        // Setup
        using var dock = new SimpleDock();

        // Act
        this.sut.DockLeft(dock);

        // Assert
        var left = this.sut.First;
        _ = left.Should().NotBeNull("adding to left for the first time should create a left group under the root");

        _ = left?.First?.Should().BeOfType<TrayGroup>("the left group should have a tray as its first part");
        _ = left?.Second?.Docks.Should().HaveCount(1).And.Contain(dock, "the dock should be added after the tray");
    }

    [TestMethod]
    public Task DockLeft_ShouldAddDockToLeft_InANewGroup()
    {
        // Setup
        using var dock1 = new SimpleDock();
        using var dock2 = new SimpleDock();

        // Act
        this.sut.DockLeft(dock1);
        this.sut.DockLeft(dock2);

        // Assert
        var left = this.sut.First;

        _ = dock1.Group.Should().NotBeNull();
        _ = this.FindParentEdgeGroup(dock1.Group!).Should().Be(left);

        _ = dock2.Group.Should().NotBeNull();
        _ = this.FindParentEdgeGroup(dock1.Group!).Should().Be(left);

        _ = dock1.Group.Should().NotBe(dock2.Group);

        return this.Verify(this.sut).UseDirectory("Snapshots");
    }

    [TestMethod]
    public void DockTop_ShouldAddBeforeCenter()
    {
        // Setup
        using var dock = new SimpleDock();

        // Act
        this.sut.DockTop(dock);

        // Assert
        var top = this.sut.First;
        _ = top.Should().NotBeNull("adding to top for the first time should create a top group under the root");

        var center = this.sut.Second;
        _ = center?.IsCenter.Should().BeTrue("the top group always comes before the center");
    }

    [TestMethod]
    public void DockTop_ShouldAddTopTray()
    {
        // Setup
        using var dock = new SimpleDock();

        // Act
        this.sut.DockTop(dock);

        // Assert
        var top = this.sut.First;
        _ = top.Should().NotBeNull("adding to top for the first time should create a top group under the root");

        _ = top?.First?.Should().BeOfType<TrayGroup>("the top group should have a tray as its first part");
        _ = top?.Second?.Docks.Should().HaveCount(1).And.Contain(dock, "the dock should be added after the tray");
    }

    [TestMethod]
    public Task DockTop_ShouldAddDockToTop_InANewGroup()
    {
        // Setup
        using var dock1 = new SimpleDock();
        using var dock2 = new SimpleDock();

        // Act
        this.sut.DockTop(dock1);
        this.sut.DockTop(dock2);

        // Assert
        var top = this.sut.First;

        _ = dock1.Group.Should().NotBeNull();
        _ = this.FindParentEdgeGroup(dock1.Group!).Should().Be(top);

        _ = dock2.Group.Should().NotBeNull();
        _ = this.FindParentEdgeGroup(dock1.Group!).Should().Be(top);

        _ = dock1.Group.Should().NotBe(dock2.Group);

        return this.Verify(this.sut).UseDirectory("Snapshots");
    }

    [TestMethod]
    public void DockRight_ShouldAddBeforeCenter()
    {
        // Setup
        using var dock = new SimpleDock();

        // Act
        this.sut.DockRight(dock);

        // Assert
        var right = this.sut.Second;
        _ = right.Should().NotBeNull("adding to right for the first time should create a right group under the root");

        var center = this.sut.First;
        _ = center?.IsCenter.Should().BeTrue("the right group always comes after the center");
    }

    [TestMethod]
    public void DockRight_ShouldAddRightTray()
    {
        // Setup
        using var dock = new SimpleDock();

        // Act
        this.sut.DockRight(dock);

        // Assert
        var right = this.sut.Second;
        _ = right?.Second?.Should().BeOfType<TrayGroup>("the right group should have a tray as its second part");
        _ = right?.First?.Docks.Should().HaveCount(1).And.Contain(dock, "the dock should be added before the tray");
    }

    [TestMethod]
    public Task DockRight_ShouldAddDockToRight_InANewGroup()
    {
        // Setup
        using var dock1 = new SimpleDock();
        using var dock2 = new SimpleDock();

        // Act
        this.sut.DockRight(dock1);
        this.sut.DockRight(dock2);

        // Assert
        var right = this.sut.Second;

        _ = dock1.Group.Should().NotBeNull();
        _ = this.FindParentEdgeGroup(dock1.Group!).Should().Be(right);

        _ = dock2.Group.Should().NotBeNull();
        _ = this.FindParentEdgeGroup(dock1.Group!).Should().Be(right);

        _ = dock1.Group.Should().NotBe(dock2.Group);

        return this.Verify(this.sut).UseDirectory("Snapshots");
    }

    [TestMethod]
    public void DockBottom_ShouldAddBeforeCenter()
    {
        // Setup
        using var dock = new SimpleDock();

        // Act
        this.sut.DockBottom(dock);

        // Assert
        var bottom = this.sut.Second;
        _ = bottom.Should()
            .NotBeNull("adding to bottom for the first time should create a bottom group under the root");

        var center = this.sut.First;
        _ = center?.IsCenter.Should().BeTrue("the bottom group always comes after the center");
    }

    [TestMethod]
    public void DockBottom_ShouldAddBottomTray()
    {
        // Setup
        using var dock = new SimpleDock();

        // Act
        this.sut.DockBottom(dock);

        // Assert
        var bottom = this.sut.Second;
        _ = bottom?.Second?.Should().BeOfType<TrayGroup>("the bottom group should have a tray as its second part");
        _ = bottom?.First?.Docks.Should().HaveCount(1).And.Contain(dock, "the dock should be added before the tray");
    }

    [TestMethod]
    public Task DockBottom_ShouldAddDockToBottom_InANewGroup()
    {
        // Setup
        using var dock1 = new SimpleDock();
        using var dock2 = new SimpleDock();

        // Act
        this.sut.DockBottom(dock1);
        this.sut.DockBottom(dock2);

        // Assert
        var bottom = this.sut.Second;

        _ = dock1.Group.Should().NotBeNull();
        _ = this.FindParentEdgeGroup(dock1.Group!).Should().Be(bottom);

        _ = dock2.Group.Should().NotBeNull();
        _ = this.FindParentEdgeGroup(dock1.Group!).Should().Be(bottom);

        _ = dock1.Group.Should().NotBe(dock2.Group);

        return this.Verify(this.sut).UseDirectory("Snapshots");
    }

    [TestMethod]
    public void RemoveGroup_RemovesTheGroup()
    {
        // Arrange
        using var dock = new SimpleDock();
        this.sut.DockLeft(dock);
        var left = this.sut.First as DockGroup;
        _ = left?.Second.Should().NotBeNull();

        // Act
        left?.RemoveGroup(left?.Second!);

        // Assert
        _ = left?.Second.Should().BeNull();
    }

    [TestMethod]
    public void RemoveGroup_ShouldNotRemoveCenter()
    {
        // Arrange
        using var dock = new SimpleDock();
        var center = this.sut.First;
        _ = center.Should().NotBeNull();

        // Act
        this.sut.RemoveGroup(center!);

        // Assert
        _ = this.sut.First.Should().NotBeNull();
    }

    private DockGroup? FindParentEdgeGroup(DockGroupBase group)
    {
        while (group.Parent != this.sut)
        {
            Debug.Assert(group.Parent != null, "group.Parent != null");
            return this.FindParentEdgeGroup(group.Parent);
        }

        return group as DockGroup;
    }
}
