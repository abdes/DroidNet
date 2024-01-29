// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Collections.ObjectModel;
using DroidNet.Docking;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

/// <summary>
/// Contains unit test cases for the <see cref="DockGroup" /> class, for
/// adding and removing groups.
/// </summary>
public partial class DockGroupTests
{
    [TestMethod]
    public void RemoveGroup_ThrowsOnLeaf()
    {
        // Arrange
        var sut = new MockDockGroup();

        // Act
        var act = () => sut.RemoveGroup(new DummyDockGroup());

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void RemoveGroup_ThrowsOnNonChild()
    {
        // Arrange
        var sut = new MockDockGroup();
        sut.SetFirst(new MockDockGroup());

        // Act
        var act = () => sut.RemoveGroup(new DummyDockGroup());

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void RemoveGroup_Child_Works()
    {
        // Arrange
        var first = new MockDockGroup();
        var second = new MockDockGroup();
        var sut = new MockDockGroup();
        sut.SetFirst(first);
        sut.SetSecond(second);

        // Act
        sut.RemoveGroup(first);
        sut.RemoveGroup(second);

        // Assert
        _ = sut.First.Should().BeNull();
        _ = sut.Second.Should().BeNull();
    }

    [TestMethod]
    public void RemoveGroup_RemovesSelfFromParent_WhenLeftWithNoChildren()
    {
        // Arrange
        var parent = new MockDockGroup();
        var sut = new MockDockGroup();
        parent.SetFirst(sut);

        var first = new MockDockGroup();
        sut.SetFirst(first);

        // Act
        sut.RemoveGroup(first);

        // Assert
        _ = sut.First.Should().BeNull();
        _ = parent.First.Should().BeNull();
    }

    private sealed class DummyDockGroup : IDockGroup
    {
        public ReadOnlyObservableCollection<IDock> Docks => throw new NotImplementedException();

        public bool IsHorizontal => throw new NotImplementedException();

        public bool IsVertical => throw new NotImplementedException();

        public bool IsEmpty => throw new NotImplementedException();

        public Orientation Orientation => throw new NotImplementedException();

        public IDockGroup First => throw new NotImplementedException();

        public IDockGroup Second => throw new NotImplementedException();
    }
}
