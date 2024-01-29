// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Detail;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

[TestClass]
[ExcludeFromCodeCoverage]
public class DockableTests
{
    [TestMethod]
    public void Title_Setter_ShouldTriggerPropertyChange_ForMinimizedTitle_WhenCurrentValueIsNull()
    {
        // Arrange
        var dockable = new Dockable("testId");
        var eventTriggered = false;

        dockable.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(Dockable.MinimizedTitle))
            {
                eventTriggered = true;
            }
        };

        // Act
        dockable.Title = "New Title";

        // Assert
        _ = eventTriggered.Should().BeTrue();

        // Set MinimizedTitle to non-null value
        dockable.MinimizedTitle = "Minimized Title";
        eventTriggered = false;

        // Act
        dockable.Title = "Another New Title";

        // Assert
        _ = eventTriggered.Should().BeFalse();
    }

    [TestMethod]
    public void Title_Setter_ShouldTriggerPropertyChange_ForTabbedTitle_WhenCurrentValueIsNull()
    {
        // Arrange
        var dockable = new Dockable("testId");
        var eventTriggered = false;

        dockable.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(Dockable.TabbedTitle))
            {
                eventTriggered = true;
            }
        };

        // Act
        dockable.Title = "New Title";

        // Assert
        _ = eventTriggered.Should().BeTrue();

        // Set TabbedTitle to non-null value
        dockable.TabbedTitle = "Tabbed Title";
        eventTriggered = false;

        // Act
        dockable.Title = "Another New Title";

        // Assert
        _ = eventTriggered.Should().BeFalse();
    }
}
