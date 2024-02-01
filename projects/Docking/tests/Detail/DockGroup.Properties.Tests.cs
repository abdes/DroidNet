// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using DroidNet.Docking;
using FluentAssertions;
using FluentAssertions.Execution;
using Microsoft.VisualStudio.TestTools.UnitTesting;

/// <summary>
/// Contains unit test cases for <see cref="DockGroup" /> properties.
/// </summary>
public partial class DockGroupTests
{
    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.Properties")]
    public void IsHorizontal_ReturnsTrueForHorizontalOrientation()
    {
        // Arrange
        var sut = new EmptyDockGroup();

        // Act
        sut.SetOrientation(Orientation.Horizontal);

        // Assert
        using (new AssertionScope())
        {
            _ = sut.IsHorizontal.Should().BeTrue();
            _ = sut.IsVertical.Should().BeFalse();
        }
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.Properties")]
    public void IsVertical_ReturnsTrueForVerticalOrientation()
    {
        // Arrange
        var sut = new EmptyDockGroup();

        // Act
        sut.SetOrientation(Orientation.Vertical);

        // Assert
        using (new AssertionScope())
        {
            _ = sut.IsHorizontal.Should().BeFalse();
            _ = sut.IsVertical.Should().BeTrue();
        }
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.Properties")]
    public void IsLeaf_ReturnsTrueForGroupWithNoChildren()
    {
        // Arrange
        var sut = new MockDockGroup();
        sut.SetFirst(null);
        sut.SetSecond(null);

        // Act
        var result = sut.IsLeaf;

        // Assert
        _ = result.Should().BeTrue();
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.Properties")]
    public void IsLeaf_ReturnsFalseWithFirstChild()
    {
        // Arrange
        var sut = new MockDockGroup();
        sut.SetFirst(new MockDockGroup());

        // Act
        var result = sut.IsLeaf;

        // Assert
        _ = result.Should().BeFalse();
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.Properties")]
    public void IsLeaf_ReturnsFalseWithSecondChild()
    {
        // Arrange
        var sut = new MockDockGroup();
        sut.SetSecond(new MockDockGroup());

        // Act
        var result = sut.IsLeaf;

        // Assert
        _ = result.Should().BeFalse();
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.Properties")]
    public void IsLeaf_ReturnsFalseWithBothChildren()
    {
        // Arrange
        var sut = new MockDockGroup();
        sut.SetFirst(new MockDockGroup());
        sut.SetSecond(new MockDockGroup());

        // Act
        var result = sut.IsLeaf;

        // Assert
        _ = result.Should().BeFalse();
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.Properties")]
    public void SetFirst_WithNull_Works()
    {
        // Arrange
        var sut = new MockDockGroup();

        // Act
        sut.SetFirst(null);

        // Assert
        _ = sut.First.Should().BeNull();
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.Properties")]
    public void SetSecond_WithNull_Works()
    {
        // Arrange
        var sut = new MockDockGroup();

        // Act
        sut.SetSecond(null);

        // Assert
        _ = sut.Second.Should().BeNull();
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.Properties")]
    public void SetSecond_Child_UpdatesChildParent()
    {
        // Arrange
        var sut = new MockDockGroup();
        var child = new MockDockGroup();

        // Act
        sut.SetSecond(child);

        // Assert
        _ = sut.Second.Should().Be(child);
        _ = child.Parent.Should().Be(sut);
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.Properties")]
    public void SetFirst_Child_UpdatesChildParent()
    {
        // Arrange
        var sut = new MockDockGroup();
        var child = new MockDockGroup();

        // Act
        sut.SetFirst(child);

        // Assert
        _ = sut.First.Should().Be(child);
        _ = child.Parent.Should().Be(sut);
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.Properties")]
    public void SetFirst_ChildWithParent_UpdatesChildParent()
    {
        // Arrange
        var otherParent = new MockDockGroup();
        var child = new MockDockGroup();
        otherParent.SetFirst(child);
        var sut = new MockDockGroup();

        // Act
        sut.SetFirst(child);

        // Assert
        _ = sut.First.Should().Be(child);
        _ = child.Parent.Should().Be(sut);
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.Properties")]
    public void SetSecond_ChildWithParent_UpdatesChildParent()
    {
        // Arrange
        var otherParent = new MockDockGroup();
        var child = new MockDockGroup();
        otherParent.SetFirst(child);
        var sut = new MockDockGroup();

        // Act
        sut.SetSecond(child);

        // Assert
        _ = sut.Second.Should().Be(child);
        _ = child.Parent.Should().Be(sut);
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.Properties")]
    public void SetFirst_ToSelf_Throws()
    {
        // Arrange
        var sut = new MockDockGroup();

        // Act
        var act = () => sut.SetFirst(sut);

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.Properties")]
    public void SetSecond_ToSelf_Throws()
    {
        // Arrange
        var sut = new MockDockGroup();

        // Act
        var act = () => sut.SetSecond(sut);

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.Properties")]
    public void SetFirst_ThrowsIfNotEmptyAndChildIsNotNull()
    {
        // Arrange
        var sut = new NonEmptyDockGroup();
        var child = new MockDockGroup();

        // Act
        var act = () => sut.SetFirst(child);

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.Properties")]
    public void SetSecond_ThrowsIfNotEmptyAndChildIsNotNull()
    {
        // Arrange
        var sut = new NonEmptyDockGroup();
        var child = new MockDockGroup();

        // Act
        var act = () => sut.SetSecond(child);

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    private class MockDockGroup : DockGroup
    {
        public void SetOrientation(Orientation orientation) => this.Orientation = orientation;

        public void SetFirst(IDockGroup? first) => this.First = first;

        public void SetSecond(IDockGroup? second) => this.Second = second;
    }

    private sealed class EmptyDockGroup : MockDockGroup;

    private sealed class NonEmptyDockGroup : MockDockGroup
    {
        private bool migrateCalled;
        private bool shouldMigrateBeCalled;

        public NonEmptyDockGroup() => this.AddDock(DummyDock.New());

        public void ExpectMigrateToBeCalled() => this.shouldMigrateBeCalled = true;

        public void Verify()
        {
            if (this.shouldMigrateBeCalled != this.migrateCalled)
            {
                throw new AssertFailedException(
                    $"was{(this.shouldMigrateBeCalled ? string.Empty : " not")} expecting {nameof(this.MigrateDocksToGroup)} to be called");
            }
        }

        protected override void MigrateDocksToGroup(DockGroup group)
        {
            base.MigrateDocksToGroup(group);
            this.migrateCalled = true;
        }
    }

    private sealed class DummyDock : Dock
    {
        private DummyDock()
        {
        }

        public static DummyDock New() => Factory.CreateDock(typeof(DummyDock)) as DummyDock ??
                                         throw new AssertFailedException($"could not create a {nameof(DummyDock)}");
    }
}
