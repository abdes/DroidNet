// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Diagnostics.CodeAnalysis;
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
        var sut = new EmptyDockGroup();
        var child = new EmptyDockGroup();
        sut.SetFirst(child);
        var newDock = DummyDock.New();

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
        // Arrange
        var sut = new NonEmptyDockGroup();
        var newDock = DummyDock.New();

        // Act
        var action = () => sut.AddDock(newDock);

        // Assert
        _ = action.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.DockManagement")]
    public void AddDock_WithNoAnchor_EmptyGroup_AddsNewDock()
    {
        // Arrange
        var sut = new EmptyDockGroup();
        var newDock = DummyDock.New();

        // Act
        sut.AddDock(newDock);

        // Assert
        _ = sut.Docks.Should().NotBeEmpty().And.OnlyContain(d => d == newDock);
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.DockManagement")]
    public void AddDockWithAnchor_WhenAnchorIsNotAChild_Throws()
    {
        // Arrange
        var sut = new NonEmptyDockGroup();
        var anchor = DummyDock.New();
        var newDock = DummyDock.New();

        // Act
        var action = () => sut.AddDock(newDock, new AnchorLeft(anchor.Id));

        // Assert
        _ = sut.Docks.Should().NotContain(d => d.Id.Equals(anchor.Id));
        _ = action.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.DockManagement")]
    [DataRow(Orientation.Horizontal)]
    [DataRow(Orientation.Vertical)]
    public void AddDockWithAnchor_SameOrientation(Orientation orientation)
    {
        // Arrange
        var sut = new NonEmptyDockGroup();
        sut.SetOrientation(orientation);
        var anchor = sut.Docks.First();
        var before = DummyDock.New();
        var after = DummyDock.New();

        // Act
        if (orientation == Orientation.Horizontal)
        {
            sut.AddDock(after, new AnchorRight(anchor.Id));
            sut.AddDock(before, new AnchorLeft(anchor.Id));
        }
        else
        {
            sut.AddDock(after, new AnchorBottom(anchor.Id));
            sut.AddDock(before, new AnchorTop(anchor.Id));
        }

        // Assert
        _ = sut.Docks.Should().HaveCount(3).And.ContainInOrder(before, anchor, after);
    }

    [TestClass]
    [TestCategory(nameof(DockGroup))]
    [ExcludeFromCodeCoverage]
    public class AddDockWithAnchorDifferentOrientation
    {
        private readonly NonEmptyDockGroup sut;
        private readonly IDock second;
        private readonly DummyDock first;
        private readonly DummyDock third;

        public AddDockWithAnchorDifferentOrientation()
        {
            this.sut = new NonEmptyDockGroup();
            this.sut.SetOrientation(Orientation.Horizontal);
            this.second = this.sut.Docks.First();
            this.first = DummyDock.New();
            this.third = DummyDock.New();
            this.sut.AddDock(this.first, new AnchorLeft(this.second.Id));
            this.sut.AddDock(this.third, new AnchorRight(this.second.Id));
        }

        [TestMethod]
        [TestCategory($"{nameof(DockGroup)}.DockManagement")]
        public void AddDockWithAnchor_DifferentOrientation_First()
        {
            // Arrange
            _ = this.sut.Docks.Should().HaveCount(3).And.ContainInOrder(this.first, this.second, this.third);

            var newDock = DummyDock.New();

            // Act
            this.sut.AddDock(newDock, new AnchorTop(this.first.Id));

            // Assert
            _ = this.sut.Orientation.Should().Be(Orientation.Horizontal);
            _ = this.sut.Docks.Should().BeEmpty();
            _ = this.sut.First.Should().NotBeNull();
            _ = this.sut.Second.Should().NotBeNull();

            _ = this.sut.First!.Orientation.Should().Be(Orientation.Vertical);
            _ = this.sut.First!.Docks.Should().ContainInOrder(newDock, this.first);

            _ = this.sut.Second!.Orientation.Should().Be(Orientation.Horizontal);
            _ = this.sut.Second!.Docks.Should().ContainInOrder(this.second, this.third);
        }

        [TestMethod]
        [TestCategory($"{nameof(DockGroup)}.DockManagement")]
        public void AddDockWithAnchor_DifferentOrientation_Last()
        {
            // Arrange
            _ = this.sut.Docks.Should().HaveCount(3).And.ContainInOrder(this.first, this.second, this.third);

            var newDock = DummyDock.New();

            // Act
            this.sut.AddDock(newDock, new AnchorBottom(this.third.Id));

            // Assert
            _ = this.sut.Orientation.Should().Be(Orientation.Horizontal);
            _ = this.sut.Docks.Should().BeEmpty();
            _ = this.sut.First.Should().NotBeNull();
            _ = this.sut.Second.Should().NotBeNull();

            _ = this.sut.First!.Orientation.Should().Be(Orientation.Horizontal);
            _ = this.sut.First!.Docks.Should().ContainInOrder(this.first, this.second);

            _ = this.sut.Second!.Orientation.Should().Be(Orientation.Vertical);
            _ = this.sut.Second!.Docks.Should().ContainInOrder(this.third, newDock);
        }

        [TestMethod]
        [TestCategory($"{nameof(DockGroup)}.DockManagement")]
        public void AddDockWithAnchor_DifferentOrientation_BeforeSecond()
        {
            // Arrange
            _ = this.sut.Docks.Should().HaveCount(3).And.ContainInOrder(this.first, this.second, this.third);

            var newDock = DummyDock.New();

            // Act
            this.sut.AddDock(newDock, new AnchorTop(this.second.Id));

            // Assert
            _ = this.sut.Orientation.Should().Be(Orientation.Horizontal);
            _ = this.sut.Docks.Should().BeEmpty();
            _ = this.sut.First.Should().NotBeNull();
            _ = this.sut.Second.Should().NotBeNull();
            var newNode = this.sut.Second!;
            _ = newNode.First.Should().NotBeNull();
            _ = newNode.Second.Should().NotBeNull();

            _ = this.sut.First!.Orientation.Should().Be(Orientation.Horizontal);
            _ = this.sut.First!.Docks.Should().ContainInOrder(this.first);

            _ = newNode.First!.Orientation.Should().Be(Orientation.Vertical);
            _ = newNode.First!.Docks.Should().ContainInOrder(newDock, this.second);

            _ = newNode.Second!.Orientation.Should().Be(Orientation.Horizontal);
            _ = newNode.Second!.Docks.Should().ContainInOrder(this.third);
        }

        [TestMethod]
        [TestCategory($"{nameof(DockGroup)}.DockManagement")]
        public void AddDockWithAnchor_DifferentOrientation_AfterSecond()
        {
            // Arrange
            _ = this.sut.Docks.Should().HaveCount(3).And.ContainInOrder(this.first, this.second, this.third);

            var newDock = DummyDock.New();

            // Act
            this.sut.AddDock(newDock, new AnchorBottom(this.second.Id));

            // Assert
            _ = this.sut.Orientation.Should().Be(Orientation.Horizontal);
            _ = this.sut.Docks.Should().BeEmpty();
            _ = this.sut.First.Should().NotBeNull();
            _ = this.sut.Second.Should().NotBeNull();
            var newNode = this.sut.Second!;
            _ = newNode.First.Should().NotBeNull();
            _ = newNode.Second.Should().NotBeNull();

            _ = this.sut.First!.Orientation.Should().Be(Orientation.Horizontal);
            _ = this.sut.First!.Docks.Should().ContainInOrder(this.first);

            _ = newNode.First!.Orientation.Should().Be(Orientation.Vertical);
            _ = newNode.First!.Docks.Should().ContainInOrder(this.second, newDock);

            _ = newNode.Second!.Orientation.Should().Be(Orientation.Horizontal);
            _ = newNode.Second!.Docks.Should().ContainInOrder(this.third);
        }
    }
}
