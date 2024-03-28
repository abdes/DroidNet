// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

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
    [TestCategory($"{nameof(DockGroup)}.GroupManagement")]
    public void RemoveGroup_ThrowsOnLeaf()
    {
        // Arrange
        var sut = new MockDockGroup(this.docker);

        // Act
        var act = () => sut.RemoveGroup(new MockDockGroup(this.docker));

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void RemoveGroup_ShouldNotRemoveCenter()
    {
        // Arrange
        using var sut = new MockDockGroup(this.docker);
        using var center = new MockDockGroup(this.docker) { IsCenter = true };
        sut.SetFirst(center);
        sut.SetSecond(center);

        // Act
        sut.RemoveGroup(center);

        // Assert
        sut.First.Should().Be(center);
        sut.Second.Should().Be(center);
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.GroupManagement")]
    public void RemoveGroup_ThrowsOnNonChild()
    {
        var act = () =>
        {
            // Arrange
            using var sut = new MockDockGroup(this.docker);
            sut.SetFirst(new MockDockGroup(this.docker));

            // Act
            sut.RemoveGroup(new MockDockGroup(this.docker));
        };

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.GroupManagement")]
    public void RemoveGroup_First_IsChild_Works()
    {
        // Arrange
        var first = new MockDockGroup(this.docker);
        var second = new MockDockGroup(this.docker);
        var sut = new MockDockGroup(this.docker);
        sut.SetFirst(first);
        sut.SetSecond(second);

        // Act
        sut.RemoveGroup(first);

        // Assert
        _ = sut.First.Should().BeNull();
        _ = sut.Second.Should().NotBeNull();
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.GroupManagement")]
    public void RemoveGroup_Second_IsChild_Works()
    {
        // Arrange
        var first = new MockDockGroup(this.docker);
        var second = new MockDockGroup(this.docker);
        var sut = new MockDockGroup(this.docker);
        sut.SetFirst(first);
        sut.SetSecond(second);

        // Act
        sut.RemoveGroup(second);

        // Assert
        _ = sut.First.Should().NotBeNull();
        _ = sut.Second.Should().BeNull();
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.GroupManagement")]
    public void RemoveGroup_InvokesOptimizerIfImplemented()
    {
        // Arrange
        var optimizingDocker = new DummyOptimizingDocker();
        var sut = new MockDockGroup(optimizingDocker);
        var first = new MockDockGroup(optimizingDocker);
        sut.SetFirst(first);

        // Act
        optimizingDocker.Reset();
        sut.RemoveGroup(first);

        // Assert
        _ = optimizingDocker.ConsolidateUpCalled.Should().BeTrue();
    }
}
