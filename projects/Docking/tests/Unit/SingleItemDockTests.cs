// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Docking.Workspace;

namespace DroidNet.Docking.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(SingleItemDock))]
public sealed class SingleItemDockTests : IDisposable
{
    private readonly Docker docker = new();
    private readonly Dockable dockable1;
    private readonly Dockable dockable2;

    public SingleItemDockTests()
    {
        this.dockable1 = this.docker.CreateDockable<Dockable>("dockable1");
        this.dockable2 = this.docker.CreateDockable<Dockable>("dockable2");
    }

    [TestCleanup]
    public void Dispose() => this.docker.Dispose();

    [TestMethod]
    public void AddDockable_WhenDockIsEmpty_ShouldAddDockable()
    {
        // Arrange
        var dock = this.docker.CreateDock<SingleItemDock>();

        // Act
        dock.AdoptDockable(this.dockable1);

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
            using var dock = this.docker.CreateDock<SingleItemDock>();
            dock.AdoptDockable(this.dockable1);

            // Act
            dock.AdoptDockable(this.dockable2);
        };

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }
}
