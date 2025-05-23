// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Tests.Mocks;
using FluentAssertions;

namespace DroidNet.Docking.Tests;

/// <summary>
/// Unit test cases for the <see cref="Anchor" /> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(AnchorTests))]
public partial class AnchorTests
{
    [TestMethod]
    public void ConstructorSetsPositionAndRelativeTo()
    {
        // Arrange
        using var mockDockable = new MockDockable("dockable");
        using var anchor = new Anchor(AnchorPosition.Left, mockDockable);

        // Assert
        _ = anchor.Position.Should().Be(AnchorPosition.Left);
        _ = anchor.RelativeTo.Should().Be(mockDockable);
    }

    [TestMethod]
    public void AnchorRelativeSubscribesToDockableOnDisposed()
    {
        // Arrange
        using var mockDockable = new MockDockable("dockable");
        using var anchor = new Anchor(AnchorPosition.Left, mockDockable);

        // Assert
        _ = mockDockable.HasSubscription().Should().BeTrue();
    }

    [TestMethod]
    public void AnchorRelativeWhenDisposedUnsubscribesFromDockable()
    {
        // Arrange
        using var mockDockable = new MockDockable("dockable");
        var anchor = new Anchor(AnchorPosition.Left, mockDockable);
        _ = mockDockable.HasSubscription().Should().BeTrue();

        // Act
        anchor.Dispose();

        // Assert
        _ = mockDockable.HasSubscription().Should().BeFalse();

        // Calling Dispose again is ok
        var act = anchor.Dispose;
        _ = act.Should().NotThrow();
    }

    [TestMethod]
    public void AnchorRelativeDockableWithAnchoredOwnerWhenDockableDisposedMovesAnchorToOwnerAnchor()
    {
        // Arrange
        using var ownerAnchor = new AnchorRight();
        using var owner = new SimpleDock();
        owner.Anchor = ownerAnchor;
        var mockDockable = new MockDockable("dockable", owner);
        using var anchor = new Anchor(AnchorPosition.Left, mockDockable);

        // Act
        mockDockable.Dispose();

        // Assert
        _ = anchor.Position.Should().Be(AnchorPosition.Right);
        _ = anchor.RelativeTo.Should().BeNull();
    }

    [TestMethod]
    public void AnchorRelativeDockableWithNoOwnerWhenDockableDisposedMovesAnchorToRootLeft()
    {
        // Arrange
        var mockDockable = new MockDockable("dockable");
        using var anchor = new Anchor(AnchorPosition.Left, mockDockable);

        // Act
        mockDockable.Dispose();

        // Assert
        _ = anchor.Position.Should().Be(AnchorPosition.Left);
        _ = anchor.RelativeTo.Should().BeNull();
    }

    [TestMethod]
    public void AnchorRelativeDockableWithOwnerNoAnchorWhenDockableDisposedMovesAnchorToRootLeft()
    {
        // Arrange
        using var ownerAnchor = new AnchorRight();
        using var owner = new SimpleDock();
        var mockDockable = new MockDockable("dockable", owner);
        using var anchor = new Anchor(AnchorPosition.Left, mockDockable);

        // Act
        mockDockable.Dispose();

        // Assert
        _ = anchor.Position.Should().Be(AnchorPosition.Left);
        _ = anchor.RelativeTo.Should().BeNull();
    }

    /// <summary>
    /// A mock for the <see cref="Dockable" /> class for testing.
    /// </summary>
    private sealed partial class MockDockable : IDockable
    {
        public MockDockable(string id, IDock? owner = null)
        {
            this.Id = id;
            this.Title = this.MinimizedTitle = this.TabbedTitle = id;
            this.Owner = owner;
        }

        public event EventHandler<EventArgs>? OnDisposed;

        public string Id { get; }

        public string Title { get; set; }

        public string MinimizedTitle { get; set; }

        public string TabbedTitle { get; set; }

        public Width PreferredWidth { get; set; } = new();

        public Height PreferredHeight { get; set; } = new();

        public object? ViewModel => null;

        public IDock? Owner { get; }

        public bool IsActive => false;

        public void Dispose() => this.OnDisposed?.Invoke(this, EventArgs.Empty);

        public bool HasSubscription() => this.OnDisposed?.GetInvocationList().Length > 0;
    }
}
