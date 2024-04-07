// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Mocks;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

/// <summary>
/// Contains unit test cases for the <see cref="DockGroup" /> class, for
/// adding and removing groups.
/// </summary>
public partial class DockGroupTests
{
    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.DockManagement")]
    public void AddDock_ToGroupWithChildren_DebugAssertFails()
    {
        // Arrange
        using var sut = new EmptyDockGroup(this.docker);
        using var child = new EmptyDockGroup(this.docker);
        sut.SetFirst(child);
        using var newDock = DummyDock.New();

        // Act
        sut.AddDock(newDock);

        // Assert
#if DEBUG
        _ = this.TraceListener.RecordedMessages.Should().Contain(m => m.StartsWith("Fail: "));
#endif
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.DockManagement")]
    public void AddDock_WithNoAnchor_CanOnlyBeUsedIfGroupIsEmpty()
    {
        var action = () =>
        {
            // Arrange
            using var sut = new NonEmptyDockGroup(this.docker);
            using var newDock = DummyDock.New();

            // Act
            sut.AddDock(newDock);
        };

        // Assert
        _ = action.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.DockManagement")]
    public void AddDock_WithNoAnchor_EmptyGroup_AddsNewDock()
    {
        // Arrange
        using var sut = new EmptyDockGroup(this.docker);
        using var newDock = DummyDock.New();

        // Act
        sut.AddDock(newDock);

        // Assert
        _ = sut.Docks.Should().HaveCount(1).And.Contain(newDock);
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.DockManagement")]
    public void AddDockWithAnchor_WhenAnchorIsNotAChild_Throws()
    {
        var action = () =>
        {
            // Arrange
            using var anchor = DummyDock.New();
            using var sut = new NonEmptyDockGroup(this.docker);
            using var anchorDockable = Dockable.New("anchor");
            anchor.AdoptDockable(anchorDockable);
            using var newDock = DummyDock.New();

            // Act
            sut.AddDock(newDock, new AnchorLeft(anchorDockable));
        };

        // Assert
        _ = action.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.DockManagement")]
    [DataRow(DockGroupOrientation.Horizontal)]
    [DataRow(DockGroupOrientation.Vertical)]
    public void AddDockWithAnchor_SameOrientation(DockGroupOrientation orientation)
    {
        // Arrange
        using var sut = new NonEmptyDockGroup(this.docker);
        sut.SetOrientation(orientation);
        var anchor = sut.Docks[0];
        using var anchorDockable = Dockable.New("anchor");
        anchor.AdoptDockable(anchorDockable);
        var before = DummyDock.New();
        var after = DummyDock.New();

        // Act
        if (orientation == DockGroupOrientation.Horizontal)
        {
            sut.AddDock(after, new AnchorRight(anchorDockable));
            sut.AddDock(before, new AnchorLeft(anchorDockable));
        }
        else
        {
            sut.AddDock(after, new AnchorBottom(anchorDockable));
            sut.AddDock(before, new AnchorTop(anchorDockable));
        }

        // Assert
        _ = sut.Docks.Should().HaveCount(3).And.ContainInOrder(before, anchor, after);
    }

    [TestClass]
    [TestCategory(nameof(DockGroup))]
    [ExcludeFromCodeCoverage]
    public class AddDockWithAnchorDifferentOrientation : IDisposable
    {
        private readonly DummyDocker docker = new();
        private readonly NonEmptyDockGroup sut;
        private readonly IDock second;
        private readonly DummyDock first;
        private readonly DummyDock third;

        public AddDockWithAnchorDifferentOrientation()
        {
            this.sut = new NonEmptyDockGroup(this.docker);
            this.sut.SetOrientation(DockGroupOrientation.Horizontal);
            this.second = this.sut.Docks[0];
            this.second.AdoptDockable(Dockable.New("second"));
            this.first = DummyDock.New();
            this.first.AdoptDockable(Dockable.New("first"));
            this.third = DummyDock.New();
            this.third.AdoptDockable(Dockable.New("third"));
            this.sut.AddDock(this.first, new AnchorLeft(this.second.Dockables[0]));
            this.sut.AddDock(this.third, new AnchorRight(this.second.Dockables[0]));
        }

        [TestCleanup]
        public void Dispose()
        {
            this.sut.Dispose();
            this.first.Dispose();
            this.second.Dispose();
            this.third.Dispose();
            this.docker.Dispose();

            GC.SuppressFinalize(this);
        }

        [TestMethod]
        [TestCategory($"{nameof(DockGroup)}.DockManagement")]
        public void AddDockWithAnchor_DifferentOrientation_First()
        {
            // Arrange
            _ = this.sut.Docks.Should().HaveCount(3).And.ContainInOrder(this.first, this.second, this.third);

            using var newDock = DummyDock.New();

            // Act
            this.sut.AddDock(newDock, new AnchorTop(this.first.Dockables[0]));

            // Assert
            _ = this.sut.Orientation.Should().Be(DockGroupOrientation.Horizontal);
            _ = this.sut.Docks.Should().BeEmpty();
            _ = this.sut.First.Should().NotBeNull();
            _ = this.sut.Second.Should().NotBeNull();

            _ = this.sut.First!.Orientation.Should().Be(DockGroupOrientation.Vertical);
            _ = this.sut.First!.Docks.Should().ContainInOrder(newDock, this.first);

            _ = this.sut.Second!.Orientation.Should().Be(DockGroupOrientation.Horizontal);
            _ = this.sut.Second!.Docks.Should().ContainInOrder(this.second, this.third);
        }

        [TestMethod]
        [TestCategory($"{nameof(DockGroup)}.DockManagement")]
        public void AddDockWithAnchor_DifferentOrientation_Last()
        {
            // Arrange
            _ = this.sut.Docks.Should().HaveCount(3).And.ContainInOrder(this.first, this.second, this.third);

            using var newDock = DummyDock.New();

            // Act
            this.sut.AddDock(newDock, new AnchorBottom(this.third.Dockables[0]));

            // Assert
            _ = this.sut.Orientation.Should().Be(DockGroupOrientation.Horizontal);
            _ = this.sut.Docks.Should().BeEmpty();
            _ = this.sut.First.Should().NotBeNull();
            _ = this.sut.Second.Should().NotBeNull();

            _ = this.sut.First!.Orientation.Should().Be(DockGroupOrientation.Horizontal);
            _ = this.sut.First!.Docks.Should().ContainInOrder(this.first, this.second);

            _ = this.sut.Second!.Orientation.Should().Be(DockGroupOrientation.Vertical);
            _ = this.sut.Second!.Docks.Should().ContainInOrder(this.third, newDock);
        }

        [TestMethod]
        [TestCategory($"{nameof(DockGroup)}.DockManagement")]
        public void AddDockWithAnchor_DifferentOrientation_BeforeSecond()
        {
            // Arrange
            _ = this.sut.Docks.Should().HaveCount(3).And.ContainInOrder(this.first, this.second, this.third);

            using var newDock = DummyDock.New();

            // Act
            this.sut.AddDock(newDock, new AnchorTop(this.second.Dockables[0]));

            // Assert
            _ = this.sut.Orientation.Should().Be(DockGroupOrientation.Horizontal);
            _ = this.sut.Docks.Should().BeEmpty();
            _ = this.sut.First.Should().NotBeNull();
            _ = this.sut.Second.Should().NotBeNull();
            var newNode = this.sut.Second!;
            _ = newNode.First.Should().NotBeNull();
            _ = newNode.Second.Should().NotBeNull();

            _ = this.sut.First!.Orientation.Should().Be(DockGroupOrientation.Horizontal);
            _ = this.sut.First!.Docks.Should().ContainInOrder(this.first);

            _ = newNode.First!.Orientation.Should().Be(DockGroupOrientation.Vertical);
            _ = newNode.First!.Docks.Should().ContainInOrder(newDock, this.second);

            _ = newNode.Second!.Orientation.Should().Be(DockGroupOrientation.Horizontal);
            _ = newNode.Second!.Docks.Should().ContainInOrder(this.third);
        }

        [TestMethod]
        [TestCategory($"{nameof(DockGroup)}.DockManagement")]
        public void AddDockWithAnchor_DifferentOrientation_AfterSecond()
        {
            // Arrange
            _ = this.sut.Docks.Should().HaveCount(3).And.ContainInOrder(this.first, this.second, this.third);

            using var newDock = DummyDock.New();

            // Act
            this.sut.AddDock(newDock, new AnchorBottom(this.second.Dockables[0]));

            // Assert
            _ = this.sut.Orientation.Should().Be(DockGroupOrientation.Horizontal);
            _ = this.sut.Docks.Should().BeEmpty();
            _ = this.sut.First.Should().NotBeNull();
            _ = this.sut.Second.Should().NotBeNull();
            var newNode = this.sut.Second!;
            _ = newNode.First.Should().NotBeNull();
            _ = newNode.Second.Should().NotBeNull();

            _ = this.sut.First!.Orientation.Should().Be(DockGroupOrientation.Horizontal);
            _ = this.sut.First!.Docks.Should().ContainInOrder(this.first);

            _ = newNode.First!.Orientation.Should().Be(DockGroupOrientation.Vertical);
            _ = newNode.First!.Docks.Should().ContainInOrder(this.second, newDock);

            _ = newNode.Second!.Orientation.Should().Be(DockGroupOrientation.Horizontal);
            _ = newNode.Second!.Docks.Should().ContainInOrder(this.third);
        }
    }
}
