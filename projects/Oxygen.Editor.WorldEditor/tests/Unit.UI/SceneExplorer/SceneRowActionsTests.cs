// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices.WindowsRuntime;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Documents;
using DroidNet.Routing;
using DroidNet.Tests;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Moq;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World.Workspace;
using Oxygen.Editor.WorldEditor.Documents.Selection;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Core.Diagnostics;
using Windows.Foundation;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.SceneExplorer;

/// <summary>Exercises the design contract on realized Scene Explorer rows and their owning commands.</summary>
[TestClass]
[TestCategory("UITest")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based test names follow the repository MSTest convention.")]
public sealed partial class SceneRowActionsTests : VisualUserInterfaceTests
{
    private static readonly string[] ButtonNames = ["EyeButton", "LockButton"];
    private static readonly bool[] HoverStates = [false, true];

    /// <summary>Gets or sets the current test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Verifies independent glyph, visibility, input, and command states on a realized node row.</summary>
    /// <param name="hidden">Whether the node is explicitly hidden in the editor.</param>
    /// <param name="locked">Whether the node is locked against editing.</param>
    /// <param name="hovered">Whether the pointer is inside the row.</param>
    /// <returns>The UI test task.</returns>
    [TestMethod]
    [DataRow(false, false, false)]
    [DataRow(false, false, true)]
    [DataRow(false, true, false)]
    [DataRow(false, true, true)]
    [DataRow(true, false, false)]
    [DataRow(true, false, true)]
    [DataRow(true, true, false)]
    [DataRow(true, true, true)]
    public Task RealizedRow_StateAndHoverMatrix_MatchesTheContract(bool hidden, bool locked, bool hovered) => EnqueueAsync(async () =>
    {
        using var host = await CreateRowHostAsync().ConfigureAwait(true);
        host.Interaction.SetHidden(host.Node.Id, hidden);
        host.Interaction.SetLocked(host.Node.Id, locked);
        host.Actions.SetRowHovered(hovered);
        await WaitForRenderAsync().ConfigureAwait(true);

        AssertSlot(Button(host.Actions, "EyeButton"), hidden, hovered);
        AssertSlot(Button(host.Actions, "LockButton"), locked, hovered);
        if (hidden || hovered)
        {
            _ = Glyph(host.Actions, "EyeGlyph").Glyph.Should().Be(hidden ? "\uED1A" : "\uE890");
        }

        if (locked || hovered)
        {
            _ = Glyph(host.Actions, "LockGlyph").Glyph.Should().Be(locked ? "\uE72E" : "\uE785");
        }

        _ = Glyph(host.Actions, "EyeGlyph").FontFamily.Source.Should().Be("Segoe Fluent Icons");
        _ = Glyph(host.Actions, "LockGlyph").FontFamily.Source.Should().Be("Segoe Fluent Icons");
        _ = Button(host.Actions, "EyeButton").Command.Should().BeSameAs(host.ViewModel.ToggleEditorHiddenCommand);
        _ = Button(host.Actions, "LockButton").Command.Should().BeSameAs(host.ViewModel.ToggleEditorLockedCommand);
        _ = Button(host.Actions, "EyeButton").CommandParameter.Should().BeSameAs(host.Adapter);
        _ = Button(host.Actions, "LockButton").CommandParameter.Should().BeSameAs(host.Adapter);
    });

    /// <summary>Verifies model notifications and descendant transitions preserve hover until the pointer leaves the row.</summary>
    /// <param name="hidden">Whether the node becomes hidden during hover.</param>
    /// <param name="locked">Whether the node becomes locked during hover.</param>
    /// <returns>The UI test task.</returns>
    [TestMethod]
    [DataRow(false, false)]
    [DataRow(false, true)]
    [DataRow(true, false)]
    [DataRow(true, true)]
    public Task StateChanges_WhileHovered_KeepBothAffordancesUntilRowExit(bool hidden, bool locked) => EnqueueAsync(async () =>
    {
        using var host = await CreateRowHostAsync().ConfigureAwait(true);
        host.Actions.UpdateRowPointerPosition(new Point(1, host.Row.ActualHeight / 2));
        host.Interaction.SetHidden(host.Node.Id, hidden);
        host.Interaction.SetLocked(host.Node.Id, locked);
        await WaitForRenderAsync().ConfigureAwait(true);

        AssertSlot(Button(host.Actions, "EyeButton"), hidden, hovered: true);
        AssertSlot(Button(host.Actions, "LockButton"), locked, hovered: true);
        host.Actions.UpdateRowPointerPosition(new Point(host.Row.ActualWidth - 1, host.Row.ActualHeight / 2));
        _ = Button(host.Actions, "EyeButton").Visibility.Should().Be(Visibility.Visible, "moving between descendants is not a row exit");
        host.Actions.UpdateRowPointerPosition(new Point(-1, host.Row.ActualHeight / 2));
        AssertSlot(Button(host.Actions, "EyeButton"), hidden, hovered: false);
        AssertSlot(Button(host.Actions, "LockButton"), locked, hovered: false);
    });

    /// <summary>Verifies selection and authored visibility do not determine editor-state affordances.</summary>
    /// <returns>The UI test task.</returns>
    [TestMethod]
    public Task Selection_AndRuntimeVisibility_DoNotControlTheEditorSlots() => EnqueueAsync(async () =>
    {
        using var host = await CreateRowHostAsync().ConfigureAwait(true);
        host.ViewModel.SelectDisplayedItem(host.Adapter, host.ViewModel.ShownItems.ToArray(), isControlDown: false, isShiftDown: false);
        host.Node.IsVisible = false;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = host.Adapter.IsSelected.Should().BeTrue();
        AssertSlot(Button(host.Actions, "EyeButton"), suppressed: false, hovered: false);
        AssertSlot(Button(host.Actions, "LockButton"), suppressed: false, hovered: false);

        host.Interaction.SetHidden(host.Node.Id, true);
        host.Interaction.SetLocked(host.Node.Id, true);
        host.ViewModel.SelectDisplayedItem(host.SecondaryAdapter, host.ViewModel.ShownItems.ToArray(), isControlDown: false, isShiftDown: false);
        await WaitForRenderAsync().ConfigureAwait(true);
        AssertSlot(Button(host.Actions, "EyeButton"), suppressed: true, hovered: false);
        AssertSlot(Button(host.Actions, "LockButton"), suppressed: true, hovered: false);
    });

    /// <summary>Verifies eye invocation affects only its node and records a non-dirtying, undoable editor-state change.</summary>
    /// <param name="locked">Whether the target node is locked before invocation.</param>
    /// <returns>The UI test task.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task EyeClick_UsesRealHistory_WithoutChangingLocksSelectionOrAuthoredVisibility(bool locked) => EnqueueAsync(async () =>
    {
        using var host = await CreateRowHostAsync().ConfigureAwait(true);
        host.Interaction.SetLocked(host.Node.Id, locked);
        host.ViewModel.SelectDisplayedItem(host.SecondaryAdapter, host.ViewModel.ShownItems.ToArray(), isControlDown: false, isShiftDown: false);
        var authoredVisibility = host.Node.IsVisible;
        host.Actions.SetRowHovered(true);
        await InvokeEyeAsync(host).ConfigureAwait(true);

        _ = host.Interaction.IsHidden(host.Node.Id).Should().BeTrue();
        _ = host.Interaction.IsHidden(host.SecondaryAdapter.AttachedObject.Id).Should().BeFalse();
        _ = host.Adapter.IsLocked.Should().Be(locked);
        _ = host.Adapter.IsSelected.Should().BeFalse();
        _ = host.SecondaryAdapter.IsSelected.Should().BeTrue();
        _ = host.Fixture.Context.History.UndoStack.Should().ContainSingle("the real command owner records one hide operation");
        _ = host.Fixture.Context.Metadata.IsDirty.Should().BeFalse();
        _ = host.Node.IsVisible.Should().Be(authoredVisibility);
        AssertSlot(Button(host.Actions, "LockButton"), locked, hovered: true);

        await host.Fixture.Context.History.UndoAsync().ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = host.Adapter.IsHiddenInEditor.Should().BeFalse();
        _ = Glyph(host.Actions, "EyeGlyph").Glyph.Should().Be("\uE890");
        await host.Fixture.Context.History.RedoAsync().ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = host.Adapter.IsHiddenInEditor.Should().BeTrue();
        _ = Glyph(host.Actions, "EyeGlyph").Glyph.Should().Be("\uED1A");
        await InvokeEyeAsync(host).ConfigureAwait(true);
        _ = host.Adapter.IsHiddenInEditor.Should().BeFalse();
        _ = host.Adapter.IsLocked.Should().Be(locked);
        host.Actions.SetRowHovered(false);
        AssertSlot(Button(host.Actions, "EyeButton"), suppressed: false, hovered: false);
        AssertSlot(Button(host.Actions, "LockButton"), locked, hovered: false);
        host.Fixture.Sync.Verify(value => value.GetDocumentScene(It.IsAny<Oxygen.Editor.World.Documents.SceneDocumentMetadata>()), Times.AtLeastOnce);
        host.Fixture.Sync.VerifyNoOtherCalls();
    });

    /// <summary>Verifies lock invocation preserves eye state, selection, authored content, and history.</summary>
    /// <param name="hidden">Whether the target node is explicitly hidden before invocation.</param>
    /// <returns>The UI test task.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task LockClick_TogglesOnlyTheAnchorLock_AndDoesNotPinTheEye(bool hidden) => EnqueueAsync(async () =>
    {
        using var host = await CreateRowHostAsync().ConfigureAwait(true);
        host.Interaction.SetHidden(host.Node.Id, hidden);
        host.ViewModel.SelectDisplayedItem(host.SecondaryAdapter, host.ViewModel.ShownItems.ToArray(), isControlDown: false, isShiftDown: false);
        host.Actions.SetRowHovered(true);
        Invoke(Button(host.Actions, "LockButton"));
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = host.Adapter.IsLocked.Should().BeTrue();
        _ = host.Adapter.IsHiddenInEditor.Should().Be(hidden);
        _ = host.SecondaryAdapter.IsLocked.Should().BeFalse();
        _ = host.Adapter.IsSelected.Should().BeFalse();
        _ = host.SecondaryAdapter.IsSelected.Should().BeTrue();
        AssertSlot(Button(host.Actions, "EyeButton"), hidden, hovered: true);
        _ = ((SolidColorBrush)Glyph(host.Actions, "LockGlyph").Foreground).Color.Should().Be(
            ((SolidColorBrush)Application.Current.Resources["AccentTextFillColorPrimaryBrush"]).Color);

        Invoke(Button(host.Actions, "LockButton"));
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = host.Adapter.IsLocked.Should().BeFalse();
        _ = host.Adapter.IsHiddenInEditor.Should().Be(hidden);
        _ = ((SolidColorBrush)Glyph(host.Actions, "LockGlyph").Foreground).Color.Should().Be(
            ((SolidColorBrush)Application.Current.Resources["TextFillColorPrimaryBrush"]).Color);
        host.Actions.SetRowHovered(false);
        AssertSlot(Button(host.Actions, "EyeButton"), hidden, hovered: false);
        AssertSlot(Button(host.Actions, "LockButton"), suppressed: false, hovered: false);
        _ = host.Fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = host.Fixture.Context.Metadata.IsDirty.Should().BeFalse();
    });

    /// <summary>Verifies both row slots paint nonempty glyphs with distinct state shapes, independent of opacity.</summary>
    /// <param name="suppressed">Whether the first capture uses the hidden and locked states.</param>
    /// <returns>The UI test task.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task Glyphs_RenderNonemptyDistinctImages_InBothStates(bool suppressed) => EnqueueAsync(async () =>
    {
        using var host = await CreateRowHostAsync().ConfigureAwait(true);
        host.Actions.SetRowHovered(true);
        host.Interaction.SetHidden(host.Node.Id, suppressed);
        host.Interaction.SetLocked(host.Node.Id, suppressed);
        await WaitForRenderAsync().ConfigureAwait(true);
        var (_, eye) = await CaptureAsync(Glyph(host.Actions, "EyeGlyph")).ConfigureAwait(true);
        var (_, lockGlyph) = await CaptureAsync(Glyph(host.Actions, "LockGlyph")).ConfigureAwait(true);
        _ = eye.Count(static alpha => alpha > 0).Should().BeGreaterThan(10);
        _ = lockGlyph.Count(static alpha => alpha > 0).Should().BeGreaterThan(10);
        var (slotsWidth, slotsAlpha) = await CaptureAsync((Grid)host.Actions.FindName("Slots")!).ConfigureAwait(true);
        _ = slotsAlpha.Where((_, index) => index % slotsWidth < slotsWidth / 2)
            .Count(static alpha => alpha > 0).Should().BeGreaterThan(10, "the eye must paint inside the realized row");
        _ = slotsAlpha.Where((_, index) => index % slotsWidth >= slotsWidth / 2)
            .Count(static alpha => alpha > 0).Should().BeGreaterThan(10, "the lock slot must not render blank or be clipped away");

        host.Interaction.SetHidden(host.Node.Id, !suppressed);
        host.Interaction.SetLocked(host.Node.Id, !suppressed);
        await WaitForRenderAsync().ConfigureAwait(true);
        var (_, oppositeEye) = await CaptureAsync(Glyph(host.Actions, "EyeGlyph")).ConfigureAwait(true);
        var (_, oppositeLock) = await CaptureAsync(Glyph(host.Actions, "LockGlyph")).ConfigureAwait(true);
        AssertDistinctGlyphShapes(eye, oppositeEye, "shown and hidden eye glyphs must have different shapes");
        AssertDistinctGlyphShapes(lockGlyph, oppositeLock, "locked and unlocked glyphs must have different shapes");
    });

    /// <summary>Verifies passive indicators leave row input untouched while hovered controls become hit-test targets.</summary>
    /// <returns>The UI test task.</returns>
    [TestMethod]
    public Task PassiveIndicators_DoNotHitTestAsButtons_ButHoveredActionsDo() => EnqueueAsync(async () =>
    {
        using var host = await CreateRowHostAsync().ConfigureAwait(true);
        host.Interaction.SetHidden(host.Node.Id, true);
        host.Interaction.SetLocked(host.Node.Id, true);
        await WaitForRenderAsync().ConfigureAwait(true);
        foreach (var hovered in HoverStates)
        {
            host.Actions.SetRowHovered(hovered);
            await WaitForRenderAsync().ConfigureAwait(true);
            foreach (var name in ButtonNames)
            {
                var button = Button(host.Actions, name);
                var point = button.TransformToVisual(host.View).TransformPoint(new Point(button.ActualWidth / 2, button.ActualHeight / 2));
                var hits = VisualTreeHelper.FindElementsInHostCoordinates(point, host.View).ToArray();
                _ = hits.Contains(button).Should().Be(hovered, "non-hovered indicators must leave the row as the input target");
            }
        }
    });

    /// <summary>Verifies inherited hiding does not replace the child's explicit eye state.</summary>
    /// <returns>The UI test task.</returns>
    [TestMethod]
    public Task InheritedEditorHiding_DoesNotPinTheChildsExplicitEyeSlot() => EnqueueAsync(async () =>
    {
        using var host = await CreateRowHostAsync().ConfigureAwait(true);
        host.Interaction.SetHidden(host.Node.Id, true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var childActions = Row(host.Tree, host.ChildAdapter).FindDescendant<SceneRowActions>()!;
        _ = host.ChildAdapter.IsEffectivelyHiddenInEditor.Should().BeTrue();
        _ = host.ChildAdapter.IsHiddenInEditor.Should().BeFalse();
        AssertSlot(Button(childActions, "EyeButton"), suppressed: false, hovered: false);
        childActions.SetRowHovered(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = Glyph(childActions, "EyeGlyph").Glyph.Should().Be("\uE890", "the eye reflects only this node's explicit hidden entry");
    });

    /// <summary>Verifies actions save and restore typed workspace state without dirtying authored content.</summary>
    /// <returns>The UI test task.</returns>
    [TestMethod]
    public Task RowActions_PersistAndRestoreWorkspaceState_WithoutDirtyingTheScene() => EnqueueAsync(async () =>
    {
        using var host = await CreateRowHostAsync().ConfigureAwait(true);
        host.Actions.SetRowHovered(true);
        await InvokeEyeAsync(host).ConfigureAwait(true);
        Invoke(Button(host.Actions, "LockButton"));
        await WaitForRenderAsync().ConfigureAwait(true);
        await host.Interaction.RestoreAsync(host.Fixture.Projects.ActiveProject!, host.Node.Scene.Id).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = host.Adapter.IsHiddenInEditor.Should().BeTrue();
        _ = host.Adapter.IsLocked.Should().BeTrue();
        _ = Glyph(host.Actions, "EyeGlyph").Glyph.Should().Be("\uED1A");
        _ = Glyph(host.Actions, "LockGlyph").Glyph.Should().Be("\uE72E");
        _ = host.Fixture.Context.Metadata.IsDirty.Should().BeFalse();
        host.Settings.Verify(
            settings => settings.SaveSettingAsync(
                WorkspaceInteractionService.Key,
                It.Is<WorkspaceInteractionService.ProjectInteraction>(state =>
                    state.Scenes.ContainsKey(host.Node.Scene.Id)
                    && state.Scenes[host.Node.Scene.Id].HiddenNodeIds.Contains(host.Node.Id)
                    && state.Scenes[host.Node.Scene.Id].LockedNodeIds.Contains(host.Node.Id)),
                It.IsAny<SettingContext>(),
                It.IsAny<IProgress<SettingsProgress>>(),
                It.IsAny<CancellationToken>()),
            Times.AtLeastOnce);
    });

    /// <summary>Verifies complete fixed-width slots remain aligned across depths and do not shift labels.</summary>
    /// <returns>The UI test task.</returns>
    [TestMethod]
    public Task Slots_ReserveFullWidth_AndAlignAcrossDepthsWithoutMovingLabels() => EnqueueAsync(async () =>
    {
        using var host = await CreateRowHostAsync().ConfigureAwait(true);
        var childRow = Row(host.Tree, host.ChildAdapter);
        var childActions = childRow.FindDescendant<SceneRowActions>()!;
        var slots = (Grid)host.Actions.FindName("Slots")!;
        var initialSlotsX = slots.TransformToVisual(host.Tree).TransformPoint(default).X;
        var label = host.Row.FindDescendant<TextBlock>(text => string.Equals(text.Name, DynamicTreeItem.ItemNamePart, StringComparison.Ordinal))!;
        var initialLabelX = label.TransformToVisual(host.Tree).TransformPoint(default).X;
        _ = slots.ActualWidth.Should().Be(64);
        _ = host.Actions.ActualWidth.Should().Be(64);
        _ = host.ChildAdapter.Depth.Should().BeGreaterThan(host.Adapter.Depth);

        host.Actions.SetRowHovered(true);
        childActions.SetRowHovered(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        foreach (var name in ButtonNames)
        {
            var button = Button(host.Actions, name);
            var childButton = Button(childActions, name);
            _ = button.ActualWidth.Should().Be(28);
            _ = button.ActualHeight.Should().Be(24);
            var center = button.TransformToVisual(host.Tree).TransformPoint(new Point(14, 12));
            var childCenter = childButton.TransformToVisual(host.Tree).TransformPoint(new Point(14, 12));
            _ = center.X.Should().BeApproximately(childCenter.X, 0.5);
            _ = button.TransformToVisual(host.Row).TransformPoint(new Point(14, 12)).Y.Should().BeApproximately(host.Row.ActualHeight / 2, 0.5);
            var bounds = button.TransformToVisual(host.Row).TransformBounds(new Rect(0, 0, 28, 24));
            _ = bounds.Right.Should().BeLessThanOrEqualTo(host.Row.ActualWidth, "the lock must not be clipped by the row");
        }

        host.Interaction.SetHidden(host.Node.Id, true);
        host.Interaction.SetLocked(host.Node.Id, true);
        host.Actions.SetRowHovered(false);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = slots.ActualWidth.Should().Be(64);
        _ = slots.TransformToVisual(host.Tree).TransformPoint(default).X.Should().BeApproximately(initialSlotsX, 0.5);
        _ = label.TransformToVisual(host.Tree).TransformPoint(default).X.Should().BeApproximately(initialLabelX, 0.5);
        _ = Grid.GetColumn(Button(host.Actions, "EyeButton")).Should().Be(0);
        _ = Grid.GetColumn(Button(host.Actions, "LockButton")).Should().Be(1);
    });

    /// <summary>Verifies non-node rows retain alignment without displaying unsupported editor-state actions.</summary>
    /// <returns>The UI test task.</returns>
    [TestMethod]
    public Task SceneAndFolderRows_ReserveSlotsWithoutFakeActions() => EnqueueAsync(async () =>
    {
        using var host = await CreateRowHostAsync().ConfigureAwait(true);
        var rootActions = Row(host.Tree, host.ViewModel.Scene!).FindDescendant<SceneRowActions>()!;
        rootActions.SetRowHovered(true);
        _ = Button(rootActions, "EyeButton").Visibility.Should().Be(Visibility.Collapsed);
        _ = Button(rootActions, "LockButton").Visibility.Should().Be(Visibility.Collapsed);
        _ = rootActions.ActualWidth.Should().Be(64);
        host.Actions.DataContext = new FolderAdapter(Guid.NewGuid(), "Folder");
        host.Actions.SetRowHovered(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = Button(host.Actions, "EyeButton").Visibility.Should().Be(Visibility.Collapsed);
        _ = Button(host.Actions, "LockButton").Visibility.Should().Be(Visibility.Collapsed);
        _ = host.Actions.ActualWidth.Should().Be(64);
    });

    /// <summary>Verifies replacement adapters update bindings and invalid contexts clear all actionable content.</summary>
    /// <returns>The UI test task.</returns>
    [TestMethod]
    public Task DataContextReplacement_UpdatesGlyphsAndCommandTargets_AndClearsInvalidContent() => EnqueueAsync(async () =>
    {
        using var host = await CreateRowHostAsync().ConfigureAwait(true);
        host.Interaction.SetHidden(host.Node.Id, true);
        host.Interaction.SetLocked(host.Node.Id, true);
        host.Actions.DataContext = host.SecondaryAdapter;
        host.Actions.SetRowHovered(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = Glyph(host.Actions, "EyeGlyph").Glyph.Should().Be("\uE890");
        _ = Glyph(host.Actions, "LockGlyph").Glyph.Should().Be("\uE785");
        _ = Button(host.Actions, "LockButton").CommandParameter.Should().BeSameAs(host.SecondaryAdapter);
        Invoke(Button(host.Actions, "LockButton"));
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = host.SecondaryAdapter.IsLocked.Should().BeTrue();

        host.Interaction.SetHidden(host.Node.Id, false);
        _ = Glyph(host.Actions, "EyeGlyph").Glyph.Should().Be("\uE890");
        host.Actions.DataContext = null;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = Button(host.Actions, "EyeButton").Visibility.Should().Be(Visibility.Collapsed);
        _ = Button(host.Actions, "LockButton").Visibility.Should().Be(Visibility.Collapsed);
        _ = Button(host.Actions, "LockButton").IsTabStop.Should().BeFalse();
    });

    /// <summary>Verifies a reused tree row targets its replacement adapter rather than the original node.</summary>
    /// <returns>The UI test task.</returns>
    [TestMethod]
    public Task RecycledTreeRow_RebindsActionsToItsReplacementAdapter() => EnqueueAsync(async () =>
    {
        using var host = await CreateRowHostAsync().ConfigureAwait(true);
        host.Interaction.SetHidden(host.Node.Id, true);
        host.Interaction.SetLocked(host.Node.Id, true);
        host.Row.ItemAdapter = host.SecondaryAdapter;
        host.Tree.UpdateLayout();
        await WaitForRenderAsync().ConfigureAwait(true);
        var actions = host.Row.FindDescendant<SceneRowActions>()!;
        _ = actions.DataContext.Should().BeSameAs(host.SecondaryAdapter);
        actions.SetRowHovered(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = Glyph(actions, "EyeGlyph").Glyph.Should().Be("\uE890");
        _ = Glyph(actions, "LockGlyph").Glyph.Should().Be("\uE785");
        Invoke(Button(actions, "LockButton"));
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = host.SecondaryAdapter.IsLocked.Should().BeTrue();
        _ = host.Adapter.IsLocked.Should().BeTrue();
    });

    /// <summary>Verifies repeated unload and reload resets hover and restores a single command invocation.</summary>
    /// <returns>The UI test task.</returns>
    [TestMethod]
    public Task UnloadReload_ResetsHoverAndReattachesCommandsWithoutDuplicatingActions() => EnqueueAsync(async () =>
    {
        using var host = await CreateRowHostAsync().ConfigureAwait(true);
        for (var cycle = 0; cycle < 2; cycle++)
        {
            host.Actions.SetRowHovered(true);
            await LoadTestContentAsync(new Grid()).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = host.Actions.IsLoaded.Should().BeFalse();
            _ = host.Actions.CommandOwner.Should().BeNull();
            host.Interaction.SetHidden(host.Node.Id, true);
            await LoadTestContentAsync(host.View).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            var actions = Row(host.Tree, host.Adapter).FindDescendant<SceneRowActions>()!;
            AssertSlot(Button(actions, "EyeButton"), suppressed: true, hovered: false);
            AssertSlot(Button(actions, "LockButton"), suppressed: false, hovered: false);
            _ = actions.CommandOwner.Should().BeSameAs(host.ViewModel);
            actions.SetRowHovered(true);
            Invoke(Button(actions, "LockButton"));
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = host.Adapter.IsLocked.Should().BeTrue("one click must toggle once even after repeated reloads");
            host.Interaction.SetLocked(host.Node.Id, false);
        }
    });

    /// <summary>Creates a realized Explorer row and its authoring fixture for UI scenarios.</summary>
    /// <returns>The disposable host with the view-model, scene and realized row.</returns>
    internal static async Task<RowHost> CreateRowHostAsync()
    {
        var settings = CreateInteractionSettings();
        var interaction = new WorkspaceInteractionService(
            settings.Object,
            Mock.Of<IOperationResultPublisher>(),
            new OperationStatusReducer());
        SceneAuthoringFixture? fixture = null;
        SceneExplorerViewModel? viewModel = null;
        try
        {
            fixture = new SceneAuthoringFixture(interaction);
            var scene = fixture.Scene;
            var secondaryNode = new SceneNode(scene) { Name = "Secondary" };
            var childNode = new SceneNode(scene) { Name = "Nested child" };
            scene.RootNodes.Add(secondaryNode);
            fixture.Node.AddChild(childNode);
            var metadata = fixture.Context.Metadata;
            _ = fixture.Documents.Setup(service => service.GetOpenDocuments(It.IsAny<WindowId>())).Returns([metadata]);
            _ = fixture.Documents.Setup(service => service.GetActiveDocumentId(It.IsAny<WindowId>())).Returns(metadata.DocumentId);
            _ = fixture.Sync.Setup(service => service.GetDocumentScene(metadata)).Returns(scene);
            _ = fixture.Sync.Setup(service => service.RegisterDocument(scene, metadata)).Returns(true);
            _ = fixture.Sync.Setup(service => service.SyncSceneWhenReadyAsync(scene, It.IsAny<CancellationToken>())).ReturnsAsync(false);
            viewModel = new SceneExplorerViewModel(
                Mock.Of<IProjectManagerService>(),
                fixture.Messenger,
                Mock.Of<IRouter>(),
                fixture.Documents.Object,
                default,
                fixture.Sync.Object,
                new SceneSelectionService(),
                fixture.Commands,
                interaction: interaction,
                projectContexts: fixture.Projects);
            await viewModel.HandleDocumentOpenedAsync(scene).ConfigureAwait(true);
            var adapter = (SceneNodeAdapter)(await viewModel.FindAdapterByNodeIdAsync(fixture.Node.Id).ConfigureAwait(true))!;
            await viewModel.ExpandItemAsync(adapter).ConfigureAwait(true);
            var secondaryAdapter = (SceneNodeAdapter)(await viewModel.FindAdapterByNodeIdAsync(secondaryNode.Id).ConfigureAwait(true))!;
            var childAdapter = (SceneNodeAdapter)(await viewModel.FindAdapterByNodeIdAsync(childNode.Id).ConfigureAwait(true))!;
            var view = new SceneExplorerView { ViewModel = viewModel };
            await LoadTestContentAsync(view).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            fixture.Sync.Invocations.Clear();
            var tree = (DynamicTree)view.FindName("ExplorerTree")!;
            var row = Row(tree, adapter);
            var host = new RowHost(fixture, viewModel, view, interaction, settings, adapter, secondaryAdapter, childAdapter, tree, row, row.FindDescendant<SceneRowActions>()!);
            fixture = null;
            viewModel = null;
            return host;
        }
        finally
        {
            viewModel?.Dispose();
            fixture?.Dispose();
        }
    }

    private static void AssertSlot(Button button, bool suppressed, bool hovered)
    {
        _ = button.Visibility.Should().Be(suppressed || hovered ? Visibility.Visible : Visibility.Collapsed);
        if (suppressed || hovered)
        {
            _ = button.Opacity.Should().BeApproximately(suppressed ? 1d : 0.6d, 0.000001);
        }

        _ = button.IsHitTestVisible.Should().Be(hovered);
        _ = button.IsTabStop.Should().Be(hovered, "passive indicators must not intercept keyboard focus");
    }

    private static Button Button(SceneRowActions actions, string name) => (Button)actions.FindName(name)!;

    private static FontIcon Glyph(SceneRowActions actions, string name) => (FontIcon)actions.FindName(name)!;

    private static DynamicTreeItem Row(DynamicTree tree, ITreeItem adapter)
        => tree.FindDescendant<DynamicTreeItem>(item => ReferenceEquals(item.ItemAdapter, adapter))!;

    private static void Invoke(Button button)
        => ((IInvokeProvider)new ButtonAutomationPeer(button).GetPattern(PatternInterface.Invoke)).Invoke();

    private static async Task InvokeEyeAsync(RowHost host)
    {
        Invoke(Button(host.Actions, "EyeButton"));
        if (host.ViewModel.ToggleEditorHiddenCommand.ExecutionTask is { } pending)
        {
            await pending.ConfigureAwait(true);
        }

        await WaitForRenderAsync().ConfigureAwait(true);
    }

    private static void AssertDistinctGlyphShapes(byte[] original, byte[] opposite, string because)
    {
        _ = original.Length.Should().Be(opposite.Length);
        var originalThreshold = original.Max() * 0.5d;
        var oppositeThreshold = opposite.Max() * 0.5d;
        _ = originalThreshold.Should().BeGreaterThan(0);
        _ = oppositeThreshold.Should().BeGreaterThan(0);
        var originalMask = original.Select(alpha => alpha > originalThreshold);
        var oppositeMask = opposite.Select(alpha => alpha > oppositeThreshold);
        var pairs = originalMask.Zip(oppositeMask).ToArray();
        var union = pairs.Count(static pair => pair.First || pair.Second);
        var difference = pairs.Count(static pair => pair.First != pair.Second);
        _ = difference.Should().BeGreaterThan(union / 20, because);
    }

    private static async Task<(int width, byte[] alpha)> CaptureAsync(FrameworkElement element)
    {
        var bitmap = new RenderTargetBitmap();
        await bitmap.RenderAsync(element);
        _ = bitmap.PixelWidth.Should().BeGreaterThan(0);
        _ = bitmap.PixelHeight.Should().BeGreaterThan(0);
        var buffer = await bitmap.GetPixelsAsync();
        var pixels = buffer.ToArray();
        return (bitmap.PixelWidth, Enumerable.Range(0, pixels.Length / 4).Select(index => pixels[(index * 4) + 3]).ToArray());
    }

    private static Mock<IEditorSettingsManager> CreateInteractionSettings()
    {
        WorkspaceInteractionService.ProjectInteraction? stored = null;
        var settings = new Mock<IEditorSettingsManager>();
        _ = settings.Setup(manager => manager.LoadSettingAsync(
                WorkspaceInteractionService.Key,
                It.IsAny<SettingContext>(),
                It.IsAny<IProgress<SettingsProgress>>(),
                It.IsAny<CancellationToken>()))
            .Returns(() => Task.FromResult(stored));
        _ = settings.Setup(manager => manager.SaveSettingAsync(
                WorkspaceInteractionService.Key,
                It.IsAny<WorkspaceInteractionService.ProjectInteraction>(),
                It.IsAny<SettingContext>(),
                It.IsAny<IProgress<SettingsProgress>>(),
                It.IsAny<CancellationToken>()))
            .Returns((
                SettingKey<WorkspaceInteractionService.ProjectInteraction> _,
                WorkspaceInteractionService.ProjectInteraction value,
                SettingContext _,
                IProgress<SettingsProgress> _,
                CancellationToken _) =>
            {
                stored = value;
                return Task.CompletedTask;
            });
        return settings;
    }

    /// <summary>Owns the fixture and realized Explorer row used by Scene Explorer UI tests.</summary>
    /// <param name="Fixture">The scene authoring fixture.</param>
    /// <param name="ViewModel">The Explorer view-model.</param>
    /// <param name="View">The loaded Explorer view.</param>
    /// <param name="Interaction">The workspace interaction state owner.</param>
    /// <param name="Settings">The settings mock that stores interaction state.</param>
    /// <param name="Adapter">The primary scene-node adapter.</param>
    /// <param name="SecondaryAdapter">A second root-node adapter.</param>
    /// <param name="ChildAdapter">A nested child-node adapter.</param>
    /// <param name="Tree">The realized Explorer tree.</param>
    /// <param name="Row">The primary realized row.</param>
    /// <param name="Actions">The primary row's action controls.</param>
    internal sealed record RowHost(
        SceneAuthoringFixture Fixture,
        SceneExplorerViewModel ViewModel,
        SceneExplorerView View,
        WorkspaceInteractionService Interaction,
        Mock<IEditorSettingsManager> Settings,
        SceneNodeAdapter Adapter,
        SceneNodeAdapter SecondaryAdapter,
        SceneNodeAdapter ChildAdapter,
        DynamicTree Tree,
        DynamicTreeItem Row,
        SceneRowActions Actions) : IDisposable
    {
        /// <summary>Gets the primary node used to create the test host.</summary>
        public SceneNode Node => this.Fixture.Node;

        /// <inheritdoc />
        public void Dispose()
        {
            this.ViewModel.Dispose();
            this.Fixture.Dispose();
        }
    }
}
