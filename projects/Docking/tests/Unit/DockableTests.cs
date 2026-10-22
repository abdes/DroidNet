// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Docking.Tests.Mocks;
using DroidNet.Docking.Workspace;

namespace DroidNet.Docking.Tests;

[TestClass]
[TestCategory(nameof(Dockable))]
[ExcludeFromCodeCoverage]
public sealed class DockableTests : IDisposable
{
    private readonly Docker docker = new();

    [TestCleanup]
    public void Dispose() => this.docker.Dispose();

    [TestMethod]
    [TestCategory($"{nameof(Dockable)}.Properties")]
    public void TitleSetterShouldTriggerPropertyChangeForMinimizedTitleWhenCurrentValueIsNull()
    {
        // Arrange
        using var dockable = this.docker.CreateDockable<Dockable>("testId");
        var eventTriggered = false;

        dockable.PropertyChanged += (_, e) =>
        {
            if (string.Equals(e.PropertyName, nameof(Dockable.MinimizedTitle), StringComparison.Ordinal))
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
    public void TitleSetterShouldTriggerPropertyChangeForTabbedTitleWhenCurrentValueIsNull()
    {
        // Arrange
        using var dockable = this.docker.CreateDockable<Dockable>("testId");
        var eventTriggered = false;

        dockable.PropertyChanged += (_, e) =>
        {
            if (string.Equals(e.PropertyName, nameof(Dockable.TabbedTitle), StringComparison.Ordinal))
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
    public void TitleSetterShouldNotTriggerPropertyChangeIfNotDifferentValue()
    {
        // Arrange
        using var dockable = this.docker.CreateDockable<Dockable>("testId");
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
    [TestCategory($"{nameof(Dockable)}.Dispose")]
    public void DisposeInvokesOnDisposedBeforeDisposing()
    {
        // Arrange
        var dockable = this.docker.CreateDockable<Dockable>("1");
        var invoked = false;
        dockable.OnDisposed += (_, _) =>
        {
            invoked = true;
            var fromId = this.docker.FindDockable("1");
            _ = fromId.Should().NotBeNull();
        };

        // Act
        dockable.Dispose();

        // Assert
        _ = invoked.Should().BeTrue();
    }

    [TestMethod]
    [TestCategory($"{nameof(Dockable)}.Properties")]
    public void IsActiveSetterThrowsIfNoOwner()
    {
        var act = () =>
        {
            // Arrange
            using var dockable = this.docker.CreateDockable<Dockable>("1");

            // Act
            dockable.IsActive = true;
        };

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    [TestCategory($"{nameof(Dockable)}.Properties")]
    public void IsActiveSetterShouldTriggerPropertyChange()
    {
        // Arrange
        using var dockable = this.docker.CreateDockable<Dockable>("testId");
        using var owner = new SimpleDock();
        dockable.Owner = owner;
        var eventTriggered = false;

        dockable.PropertyChanged += (_, e) =>
        {
            if (string.Equals(e.PropertyName, nameof(Dockable.IsActive), StringComparison.Ordinal))
            {
                eventTriggered = true;
            }
        };

        // Act
        dockable.IsActive = true;

        // Assert
        _ = dockable.IsActive.Should().BeTrue();
        _ = eventTriggered.Should().BeTrue();
    }

    [TestMethod]
    [TestCategory($"{nameof(Dockable)}.Properties")]
    public void IsActiveSetterShouldNotTriggerPropertyChangeIfNotDifferentValue()
    {
        // Arrange
        using var dockable = this.docker.CreateDockable<Dockable>("testId");
        using var owner = new SimpleDock();
        dockable.Owner = owner;
        var eventTriggered = false;

        dockable.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName is nameof(Dockable.IsActive))
            {
                eventTriggered = true;
            }
        };

        // Act
        dockable.IsActive = false;

        // Assert
        _ = eventTriggered.Should().BeFalse();
    }
}
