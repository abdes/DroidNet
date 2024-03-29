// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using FluentAssertions.Execution;
using Microsoft.VisualStudio.TestTools.UnitTesting;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(SingleItemDock))]
public class SingleItemDockTests
{
    private readonly Dockable dockable1
        = Dockable.New("dockable1") ?? throw new AssertionFailedException("could not allocate a new object");

    private readonly Dockable dockable2
        = Dockable.New("dockable2") ?? throw new AssertionFailedException("could not allocate a new object");

    [TestCleanup]
    public void Dispose()
    {
        this.dockable1.Dispose();
        this.dockable2.Dispose();
    }

    [TestMethod]
    public void AddDockable_WhenDockIsEmpty_ShouldAddDockable()
    {
        // Arrange
        var dock = SingleItemDock.New() ?? throw new AssertionFailedException("could not allocate a new object");

        // Act
        dock.AddDockable(this.dockable1);

        // Assert
        _ = dock.Dockables.Should()
            .ContainSingle()
            .Which.Should()
            .Be(this.dockable1, "we added it to the dock");
    }

    [TestMethod]
    public void AddDockable_WhenDockIsNotEmpty_ShouldThrowInvalidOperationException()
    {
        var act = () =>
        {
            // Arrange
            using var dock = SingleItemDock.New() ??
                             throw new AssertionFailedException("could not allocate a new object");
            dock.AddDockable(this.dockable1);

            // Act
            dock.AddDockable(this.dockable2);
        };

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }
}
