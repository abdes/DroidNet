// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Detail;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

[TestClass]
[TestCategory(nameof(Dockable))]
[ExcludeFromCodeCoverage]
public class DockableTests
{
    private readonly Dockable dockable = Dockable.New("testId")!;

    [TestCleanup]
    public void Cleanup() => this.dockable.Dispose();

    [TestMethod]
    [TestCategory($"{nameof(Dockable)}.Properties")]
    public void Title_Setter_ShouldTriggerPropertyChange_ForMinimizedTitle_WhenCurrentValueIsNull()
    {
        // Arrange
        var eventTriggered = false;

        this.dockable.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(Dockable.MinimizedTitle))
            {
                eventTriggered = true;
            }
        };

        // Act
        this.dockable.Title = "New Title";

        // Assert
        _ = eventTriggered.Should().BeTrue();

        // Set MinimizedTitle to non-null value
        this.dockable.MinimizedTitle = "Minimized Title";
        eventTriggered = false;

        // Act
        this.dockable.Title = "Another New Title";

        // Assert
        _ = eventTriggered.Should().BeFalse();
    }

    [TestMethod]
    [TestCategory($"{nameof(Dockable)}.Properties")]
    public void Title_Setter_ShouldTriggerPropertyChange_ForTabbedTitle_WhenCurrentValueIsNull()
    {
        // Arrange
        var eventTriggered = false;

        this.dockable!.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(Dockable.TabbedTitle))
            {
                eventTriggered = true;
            }
        };

        // Act
        this.dockable.Title = "New Title";

        // Assert
        _ = eventTriggered.Should().BeTrue();

        // Set TabbedTitle to non-null value
        this.dockable.TabbedTitle = "Tabbed Title";
        eventTriggered = false;

        // Act
        this.dockable.Title = "Another New Title";

        // Assert
        _ = eventTriggered.Should().BeFalse();
    }
}
