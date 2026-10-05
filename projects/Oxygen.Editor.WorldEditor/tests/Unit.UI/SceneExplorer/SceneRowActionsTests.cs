// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World;
using Oxygen.Editor.World.SceneExplorer;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.SceneExplorer;

/// <summary>Real-control behavior of the Scene Explorer row slots: quiet default, loud suppressed, fixed geometry.</summary>
[TestClass]
[TestCategory("UITest")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed partial class SceneRowActionsTests : VisualUserInterfaceTests
{
    /// <summary>Gets or sets the current test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public Task DefaultState_ShowsNoIcons_UntilHovered() => EnqueueAsync(async () =>
    {
        var adapter = CreateAdapter(out _);
        var view = new SceneRowActions { DataContext = adapter };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        var eye = (Button)view.FindName("EyeButton")!;
        var lockButton = (Button)view.FindName("LockButton")!;

        // Quiet default: a shown, unlocked row is empty until the pointer enters the row.
        _ = eye.Visibility.Should().Be(Visibility.Collapsed);
        _ = lockButton.Visibility.Should().Be(Visibility.Collapsed);
    });

    [TestMethod]
    public Task EditorHidden_PinsTheEye_WithoutHover() => EnqueueAsync(async () =>
    {
        var adapter = CreateAdapter(out _);
        adapter.IsHiddenInEditor = true;
        var view = new SceneRowActions { DataContext = adapter };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        var eye = (Button)view.FindName("EyeButton")!;
        var lockButton = (Button)view.FindName("LockButton")!;

        // Suppressed eye is a permanent warning; the lock slot stays quiet and independent.
        _ = eye.Visibility.Should().Be(Visibility.Visible, "hidden state pins its icon regardless of hover");
        _ = eye.Opacity.Should().Be(1d, "the suppressed state is loud");
        _ = lockButton.Visibility.Should().Be(Visibility.Collapsed, "hiding must not pin the sibling lock");
    });

    [TestMethod]
    public Task Locked_PinsTheLock_WithoutHover() => EnqueueAsync(async () =>
    {
        var adapter = CreateAdapter(out _);
        adapter.IsLocked = true;
        var view = new SceneRowActions { DataContext = adapter };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        var eye = (Button)view.FindName("EyeButton")!;
        var lockButton = (Button)view.FindName("LockButton")!;

        _ = lockButton.Visibility.Should().Be(Visibility.Visible, "locked state pins its icon regardless of hover");
        _ = eye.Visibility.Should().Be(Visibility.Collapsed, "locking must not pin the sibling eye");
    });

    [TestMethod]
    public Task SlotGeometry_DoesNotCollapseWhenStatesChange() => EnqueueAsync(async () =>
    {
        var adapter = CreateAdapter(out _);
        var view = new SceneRowActions { DataContext = adapter };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        var widthWhenQuiet = view.ActualWidth;

        // Pinning both slots (icon visible) must reserve the same two fixed columns; icons appear
        // inside reserved space and never shift or collapse the row.
        adapter.IsHiddenInEditor = true;
        adapter.IsLocked = true;
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = view.ActualWidth.Should().Be(widthWhenQuiet, "revealing icons must not change slot geometry");
    });

    [TestMethod]
    public void ComputeSlotVisibility_ImplementsTheFullQuietLoudMatrix()
    {
        // default, no hover: both quiet
        var quiet = SceneRowActions.ComputeSlotVisibility(hidden: false, locked: false, hovered: false);
        _ = quiet.EyeVisible.Should().BeFalse();
        _ = quiet.LockVisible.Should().BeFalse();

        // default, hover: both affordances appear
        var hover = SceneRowActions.ComputeSlotVisibility(hidden: false, locked: false, hovered: true);
        _ = hover.EyeVisible.Should().BeTrue();
        _ = hover.LockVisible.Should().BeTrue();
        _ = hover.EyeSuppressed.Should().BeFalse("hover-revealed default is muted, not a warning");

        // suppressed states pin independently and ignore hover
        var hidden = SceneRowActions.ComputeSlotVisibility(hidden: true, locked: false, hovered: false);
        _ = hidden.EyeVisible.Should().BeTrue();
        _ = hidden.LockVisible.Should().BeFalse("hiding never pins the lock slot");
        _ = hidden.EyeSuppressed.Should().BeTrue();

        var locked = SceneRowActions.ComputeSlotVisibility(hidden: false, locked: true, hovered: false);
        _ = locked.LockVisible.Should().BeTrue();
        _ = locked.EyeVisible.Should().BeFalse("locking never pins the eye slot");
        _ = locked.LockSuppressed.Should().BeTrue();

        // both suppressed
        var both = SceneRowActions.ComputeSlotVisibility(hidden: true, locked: true, hovered: false);
        _ = (both.EyeVisible && both.LockVisible && both.EyeSuppressed && both.LockSuppressed).Should().BeTrue();
    }

    private static SceneNodeAdapter CreateAdapter(out Scene scene)
    {
        scene = new Scene(null!) { Name = "Test Scene" };
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        return new SceneNodeAdapter(node);
    }
}
