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
    [TestMethod]
    [TestCategory($"{nameof(Dockable)}.Properties")]
    public void Title_Setter_ShouldTriggerPropertyChange_ForMinimizedTitle_WhenCurrentValueIsNull()
    {
        // Arrange
        using var dockable = Dockable.New("testId")!;
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
        _ = dockable.Title.Should().Be("New Title");
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
    [TestCategory($"{nameof(Dockable)}.Properties")]
    public void Title_Setter_ShouldTriggerPropertyChange_ForTabbedTitle_WhenCurrentValueIsNull()
    {
        // Arrange
        using var dockable = Dockable.New("testId")!;
        var eventTriggered = false;

        dockable!.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(Dockable.TabbedTitle))
            {
                eventTriggered = true;
            }
        };

        // Act
        dockable.Title = "New Title";

        // Assert
        _ = dockable.Title.Should().Be("New Title");
        _ = eventTriggered.Should().BeTrue();

        // Set TabbedTitle to non-null value
        dockable.TabbedTitle = "Tabbed Title";
        eventTriggered = false;

        // Act
        dockable.Title = "Another New Title";

        // Assert
        _ = eventTriggered.Should().BeFalse();
    }

    [TestMethod]
    [TestCategory($"{nameof(Dockable)}.Properties")]
    public void Title_Setter_ShouldNotTriggerPropertyChange_IfNotDifferentValue()
    {
        // Arrange
        using var dockable = Dockable.New("testId")!;
        dockable.Title = "Title";
        var eventTriggered = false;

        dockable.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName is nameof(Dockable.TabbedTitle) or nameof(Dockable.MinimizedTitle))
            {
                eventTriggered = true;
            }
        };

        // Act
        dockable.Title = "Title";

        // Assert
        _ = eventTriggered.Should().BeFalse();
    }

    [TestMethod]
    [TestCategory($"{nameof(Dockable)}.Collection")]
    public void All_ReturnsAllManagedDockables()
    {
        // Arrange
        using var dockable1 = Dockable.New("1");
        using var dockable2 = Dockable.New("2");

        // Assert
        _ = Dockable.All.Should().HaveCount(2).And.Contain(dockable1).And.Contain(dockable2);
    }

    [TestMethod]
    [TestCategory($"{nameof(Dockable)}.Collection")]
    public void FromId_ReturnsDockable_WhenIdIsManaged()
    {
        // Arrange
        using var dockable = Dockable.New("1");

        // Act
        var fromId = Dockable.FromId("1");

        // Assert
        _ = fromId.Should().Be(dockable);
    }

    [TestMethod]
    [TestCategory($"{nameof(Dockable)}.Collection")]
    public void FromId_ReturnsNull_WhenIdIsIsNotManaged()
    {
        // Arrange
        using var dockable = Dockable.New("1");

        // Act
        var fromId = Dockable.FromId("__");

        // Assert
        _ = fromId.Should().BeNull();
    }

    [TestMethod]
    [TestCategory($"{nameof(Dockable)}.Dispose")]
    public void Dispose_InvokesOnDisposed_BeforeDisposing()
    {
        // Arrange
        var dockable = Dockable.New("1");
        var invoked = false;
        dockable.OnDisposed += () =>
        {
            invoked = true;
            var fromId = Dockable.FromId("1");
            _ = fromId.Should().NotBeNull();
        };

        // Act
        dockable.Dispose();

        // Assert
        _ = invoked.Should().BeTrue();
    }
}
