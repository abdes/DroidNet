// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Mocks;
using DroidNet.Docking.Workspace;
using DroidNet.TestHelpers;
using FluentAssertions;

/// <summary>
/// Unit test cases for the <see cref="Docker" /> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(Docker))]
public partial class DockerTests : TestSuiteWithAssertions, IDisposable
{
    private readonly Docker sut = new();

    [TestMethod]
    public void Dock_Works_WhenStateIsNotUnDocked()
    {
        // Setup
        using var dock = new SimpleDock();
        this.sut.Dock(dock, new AnchorLeft());

        // Act
        this.sut.Dock(dock, new AnchorRight());

        // Assert
        _ = dock.Group.Should().NotBeNull();
        _ = dock.Anchor.Should().BeEquivalentTo(new AnchorRight());
    }

    [TestMethod]
    public void Dock_WhenAnchorNotInGroup_Throws()
    {
        var act = () =>
        {
            // Setup
            using var anchorDock = new SimpleDock();
            anchorDock.AdoptDockable(Dockable.New("anchor"));
            using var anchor = new AnchorLeft(anchorDock.Dockables[0]);

            using var dock = new SimpleDock();

            // Act
            this.sut.Dock(dock, anchor);
        };

        // Act
        _ = act.Should()
            .Throw<InvalidOperationException>()
            .Which.Message.Contains("does not belong to a group", StringComparison.Ordinal);
    }

    [TestMethod]
    [DataRow(true, DockingState.Minimized)]
    [DataRow(false, DockingState.Pinned)]
    public void Dock_WhenDone(bool minimized, DockingState state)
    {
        // Setup
        using var anchorDock = new SimpleDock();
        this.sut.Dock(anchorDock, new AnchorLeft());
        anchorDock.AdoptDockable(Dockable.New("anchor"));
        using var anchor = new AnchorLeft(anchorDock.Dockables[0]);
        using var dock = new SimpleDock();

        var layoutChangedCalled = false;
        LayoutChangeReason? layoutChangeReason = null;
        this.sut.LayoutChanged += (_, args) =>
        {
            layoutChangedCalled = true;
            layoutChangeReason = args.Reason;
        };

        // Act
        this.sut.Dock(dock, anchor, minimized);

        // Assert
        _ = dock.State.Should().Be(state);
        _ = dock.Group.Should().Be(anchorDock.Group);
        _ = dock.Anchor.Should().Be(anchor);
        _ = layoutChangedCalled.Should().BeTrue();
        _ = layoutChangeReason.Should().Be(LayoutChangeReason.Docking);
    }

    [TestMethod]
    public void DockToCenter_WhenDockCanMinimize_Asserts()
    {
        // Setup
        using var dock = new NoCloseDock();

        // Act
        this.sut.Dock(dock, new Anchor(AnchorPosition.Center));
#if DEBUG
        _ = this.TraceListener.RecordedMessages.Should().Contain(message => message.StartsWith("Fail: "));
#endif
    }

    [TestMethod]
    public void DockToCenter_WhenDockCanClose_Asserts()
    {
        // Setup
        using var dock = new NoMinimizeDock();

        // Act
        this.sut.Dock(dock, new Anchor(AnchorPosition.Center));
#if DEBUG
        _ = this.TraceListener.RecordedMessages.Should().Contain(message => message.StartsWith("Fail: "));
#endif
    }

    [TestMethod]
    public void DockToCenter_InvokesLayoutChanged()
    {
        // Setup
        using var dock = new SimpleCenterDock();

        var layoutChangedCalled = false;
        LayoutChangeReason? layoutChangeReason = null;
        this.sut.LayoutChanged += (_, args) =>
        {
            layoutChangedCalled = true;
            layoutChangeReason = args.Reason;
        };

        // Act
        this.sut.Dock(dock, new Anchor(AnchorPosition.Center));

        // Assert
        _ = layoutChangedCalled.Should().BeTrue();
        _ = layoutChangeReason.Should().Be(LayoutChangeReason.Docking);
    }

    [TestMethod]
    public void DockToRoot_Works_WhenStateIsNotUnDocked()
    {
        // Setup
        using var dock = new SimpleDock();
        this.sut.Dock(dock, new AnchorLeft());
        _ = dock.State.Should().NotBe(DockingState.Undocked);

        // Act
        this.sut.Dock(dock, new AnchorLeft());

        _ = dock.Group.Should().NotBeNull();
        _ = dock.Anchor.Should().BeEquivalentTo(new Anchor(AnchorPosition.Left));
    }

    [TestMethod]
    [DataRow(AnchorPosition.Left, true, DockingState.Minimized)]
    [DataRow(AnchorPosition.Right, true, DockingState.Minimized)]
    [DataRow(AnchorPosition.Top, true, DockingState.Minimized)]
    [DataRow(AnchorPosition.Bottom, true, DockingState.Minimized)]
    [DataRow(AnchorPosition.Left, false, DockingState.Pinned)]
    [DataRow(AnchorPosition.Right, false, DockingState.Pinned)]
    [DataRow(AnchorPosition.Top, false, DockingState.Pinned)]
    [DataRow(AnchorPosition.Bottom, false, DockingState.Pinned)]
    [DataRow(AnchorPosition.Center, false, DockingState.Pinned)]
    public void DockToRoot_WhenDone(AnchorPosition position, bool minimized, DockingState state)
    {
        // Setup
        using var dock = new SimpleDock();

        // Act
        this.sut.Dock(dock, new Anchor(position), minimized);

        // Assert
        _ = dock.State.Should().Be(state);
        _ = dock.Group.Should().NotBeNull();
        _ = dock.Anchor.Should().BeEquivalentTo(new Anchor(position));
    }

    [TestMethod]
    public void MinimizeDock_WhenCanMinimizeIsFalse_Throws()
    {
        var act = () =>
        {
            // Setup
            using var dock = new NoMinimizeDock();

            // Act
            this.sut.MinimizeDock(dock);
        };

        // Assert
        _ = act.Should().Throw<InvalidOperationException>("dock cannot be minimized");
    }

    [TestMethod]
    public void MinimizeDock_WhenNoTray_Throws()
    {
        var act = () =>
        {
            // Setup
            using var dock = new SimpleDock();

            // Act
            this.sut.MinimizeDock(dock);
        };

        // Assert
        _ = act.Should().Throw<InvalidOperationException>("dock cannot be minimized");
    }

    [TestMethod]
    [DataRow(DockingState.Pinned)]
    [DataRow(DockingState.Minimized)]
    [DataRow(DockingState.Floating)]
    [DataRow(DockingState.Undocked)]
    public void MinimizeDock_MinimizesDock(DockingState currentState)
    {
        // Setup
        using var dock = new SimpleDock();
        this.sut.Dock(dock, new AnchorLeft());

        dock.State = currentState;

        var layoutChangedCalled = false;
        LayoutChangeReason? layoutChangeReason = null;
        this.sut.LayoutChanged += (_, args) =>
        {
            layoutChangedCalled = true;
            layoutChangeReason = args.Reason;
        };

        // Act
        this.sut.MinimizeDock(dock);

        // Assert
        _ = dock.State.Should().Be(DockingState.Minimized);
        if (currentState == DockingState.Minimized)
        {
            return;
        }

        _ = layoutChangedCalled.Should().BeTrue();
        _ = layoutChangeReason.Should()
            .Be(
                currentState == DockingState.Floating
                    ? LayoutChangeReason.Floating
                    : LayoutChangeReason.Docking);
    }

    [TestMethod]
    [DataRow(DockingState.Pinned)]
    [DataRow(DockingState.Minimized)]
    [DataRow(DockingState.Floating)]
    [DataRow(DockingState.Undocked)]
    public void PinDock_PinsDockAndRemovesFromTray(DockingState currentState)
    {
        // Setup
        using var dock = new SimpleDock();
        this.sut.Dock(dock, new AnchorLeft());
        if (currentState is DockingState.Minimized or DockingState.Floating)
        {
            this.sut.MinimizeDock(dock);
        }

        dock.State = currentState;

        var layoutChangedCalled = false;
        LayoutChangeReason? layoutChangeReason = null;
        this.sut.LayoutChanged += (_, args) =>
        {
            layoutChangedCalled = true;
            layoutChangeReason = args.Reason;
        };

        // Act
        this.sut.PinDock(dock);

        // Assert
        _ = dock.State.Should().Be(DockingState.Pinned);
        if (currentState == DockingState.Pinned)
        {
            _ = layoutChangedCalled.Should().BeFalse();
            return;
        }

        _ = layoutChangedCalled.Should().BeTrue();
        _ = layoutChangeReason.Should().Be(LayoutChangeReason.Docking);
    }

    [TestMethod]
    [DataRow(DockingState.Pinned, false)]
    [DataRow(DockingState.Minimized, false)]
    [DataRow(DockingState.Floating, false)]
    [DataRow(DockingState.Undocked, false)]
    [DataRow(DockingState.Pinned, true)]
    [DataRow(DockingState.Minimized, true)]
    [DataRow(DockingState.Floating, true)]
    [DataRow(DockingState.Undocked, true)]
    public void ResizeDock_TriggersLayoutChanged_OnlyIfDockIsPinnedAndSizeChanges(
        DockingState currentState,
        bool changeSize)
    {
        // Setup
        using var dock = new SimpleDock();
        this.sut.Dock(dock, new AnchorLeft());

        if (currentState is DockingState.Minimized or DockingState.Floating)
        {
            this.sut.MinimizeDock(dock);
        }

        dock.State = currentState;

        var layoutChangedCalled = false;
        LayoutChangeReason? layoutChangeReason = null;
        this.sut.LayoutChanged += (_, args) =>
        {
            layoutChangedCalled = true;
            layoutChangeReason = args.Reason;
        };

        // Act
        this.sut.ResizeDock(
            dock,
            changeSize ? new Width(645) : null,
            changeSize ? new Height(876) : null);

        // Assert
        if (currentState == DockingState.Pinned && changeSize)
        {
            _ = layoutChangedCalled.Should().BeTrue();
            _ = layoutChangeReason.Should().Be(LayoutChangeReason.Resize);
        }
        else
        {
            _ = layoutChangedCalled.Should().BeFalse();
        }
    }

    [TestMethod]
    public void ResizeDock_WhenSameSize_NoLayoutChange()
    {
        // Setup
        using var dock = new SimpleDock();
        this.sut.Dock(dock, new AnchorLeft());
        dock.State = DockingState.Pinned;
        dock.Width = new Width(200);
        dock.Height = new Height(700);

        var layoutChangedCalled = false;
        this.sut.LayoutChanged += (_, _) => layoutChangedCalled = true;

        // Act
        this.sut.ResizeDock(dock, new Width(200), new Height(700));

        // Assert
        _ = layoutChangedCalled.Should().BeFalse();
    }

    [TestMethod]
    public void CloseDock_WhenCanCloseIsFalse_Throws()
    {
        var act = () =>
        {
            // Setup
            using var dock = new NoCloseDock();
            dock.State = DockingState.Pinned;

            // Act
            this.sut.CloseDock(dock);
        };

        // Assert
        _ = act.Should().Throw<InvalidOperationException>("dock cannot be closed");
    }

    [TestMethod]
    [DataRow(DockingState.Pinned)]
    [DataRow(DockingState.Minimized)]
    [DataRow(DockingState.Floating)]
    [DataRow(DockingState.Undocked)]
    public void CloseDock_ClosesDock(DockingState currentState)
    {
        // Setup
        using var dock = new SimpleDock();
        this.sut.Dock(dock, new AnchorLeft());
        if (currentState is DockingState.Minimized or DockingState.Floating)
        {
            this.sut.MinimizeDock(dock);
        }

        dock.State = currentState;

        var layoutChangedCalled = false;
        LayoutChangeReason? layoutChangeReason = null;
        this.sut.LayoutChanged += (_, args) =>
        {
            layoutChangedCalled = true;
            layoutChangeReason = args.Reason;
        };

        // Act
        this.sut.CloseDock(dock);

        // Assert
        _ = dock.State.Should().Be(DockingState.Undocked);
        if (currentState == DockingState.Undocked)
        {
            return;
        }

        _ = layoutChangedCalled.Should().BeTrue();
        _ = layoutChangeReason.Should().Be(LayoutChangeReason.Docking);
    }

    [TestMethod]
    [DataRow(DockingState.Pinned)]
    [DataRow(DockingState.Undocked)]
    public void FloatDock_WhenStateIsNotMinimizedOrFloating_Throws(DockingState currentState)
    {
        var act = () =>
        {
            // Setup
            using var dock = new NoCloseDock();
            dock.State = currentState;

            // Act
            this.sut.FloatDock(dock);
        };

        // Assert
        _ = act.Should().Throw<InvalidOperationException>("dock cannot be closed");
    }

    [TestMethod]
    [DataRow(DockingState.Minimized)]
    [DataRow(DockingState.Floating)]
    public void FloatDock_FloatsDock(DockingState currentState)
    {
        // Setup
        using var dock = new SimpleDock();
        this.sut.Dock(dock, new AnchorLeft());
        if (currentState is DockingState.Minimized or DockingState.Floating)
        {
            this.sut.MinimizeDock(dock);
        }

        dock.State = currentState;

        var layoutChangedCalled = false;
        LayoutChangeReason? layoutChangeReason = null;
        this.sut.LayoutChanged += (_, args) =>
        {
            layoutChangedCalled = true;
            layoutChangeReason = args.Reason;
        };

        // Act
        this.sut.FloatDock(dock);

        // Assert
        _ = dock.State.Should().Be(DockingState.Floating);
        if (currentState == DockingState.Floating)
        {
            return;
        }

        _ = layoutChangedCalled.Should().BeTrue();
        _ = layoutChangeReason.Should().Be(LayoutChangeReason.Floating);
    }

    [TestCleanup]
    public void Cleanup()
    {
        this.Dispose();

        this.sut.Dispose();
    }
}
