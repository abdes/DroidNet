// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Dialogs;
using DroidNet.Aura.Windowing;
using DroidNet.Tests;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.Documents;
using Oxygen.Editor.World.Documents;

namespace Oxygen.Editor.World.Tests;

/// <summary>Exercises the inline conflict controls and explicit discard confirmation.</summary>
[TestClass]
[TestCategory("UITest")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository's discovery configuration.")]
public sealed class DocumentConflictControlTests : VisualUserInterfaceTests
{
    /// <summary>Overlapping Save requests share one dialog and later conflicts can open another.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task ConcurrentConflictPromptsShareTheCurrentDialog() => EnqueueAsync(async () =>
    {
        var windowId = new WindowId(1);
        var window = new Mock<IManagedWindow>();
        _ = window.SetupGet(value => value.DispatcherQueue).Returns(VisualUserInterfaceTestsApp.DispatcherQueue);
        var windows = new Mock<IWindowManagerService>();
        _ = windows.Setup(value => value.GetWindow(windowId)).Returns(window.Object);
        var closed = new TaskCompletionSource<DialogButton>(TaskCreationOptions.RunContinuationsAsynchronously);
        var dialogs = new Mock<IDialogService>();
        _ = dialogs.Setup(value => value.ShowAsync(It.IsAny<DialogSpec>(), windowId, It.IsAny<CancellationToken>())).Returns(closed.Task);
        var prompt = new DocumentConflictPrompt(dialogs.Object, windows.Object);
        var metadata = new SceneDocumentMetadata(Guid.NewGuid());
        var participant = Mock.Of<IDocumentConflictParticipant>();

        var first = prompt.ShowAsync(windowId, metadata, participant);
        var second = prompt.ShowAsync(windowId, metadata, participant);
        _ = second.Should().BeSameAs(first);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        dialogs.Verify(value => value.ShowAsync(It.IsAny<DialogSpec>(), windowId, It.IsAny<CancellationToken>()), Times.Once);
        closed.SetResult(DialogButton.Close);
        await first.ConfigureAwait(true);
        await second.ConfigureAwait(true);
        await prompt.ShowAsync(windowId, metadata, participant).ConfigureAwait(true);
        dialogs.Verify(value => value.ShowAsync(It.IsAny<DialogSpec>(), windowId, It.IsAny<CancellationToken>()), Times.Exactly(2));
    });

    /// <summary>Reload requires a second explicit discard action before touching the document.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task InlineReloadRequiresDiscardConfirmation() => EnqueueAsync(async () =>
    {
        var metadata = new SceneDocumentMetadata(Guid.NewGuid()) { IsDirty = true, Title = "Unsaved scene" };
        var participant = new Mock<IDocumentConflictParticipant>();
        _ = participant.SetupGet(value => value.HasSaveConflict).Returns(value: true);
        _ = participant.Setup(value => value.ReloadFromDiskAsync()).Returns(() =>
        {
            metadata.IsDirty = false;
            return Task.FromResult(new DocumentConflictResult(Succeeded: true, "Reloaded"));
        });
        var item = new DocumentCloseItem(metadata, () => Task.FromResult(false), participant.Object);
        var panel = new DocumentConflictPanel(item, () => { });
        await LoadTestContentAsync(panel).ConfigureAwait(true);

        await InvokeButtonAsync(panel, "Reload").ConfigureAwait(true);
        participant.Verify(value => value.ReloadFromDiskAsync(), Times.Never);
        _ = panel.FindDescendants().OfType<TextBlock>().Should().Contain(value => value.Text.Contains("Undo/Redo", StringComparison.Ordinal));
        await InvokeButtonAsync(panel, "Keep changes").ConfigureAwait(true);
        participant.Verify(value => value.ReloadFromDiskAsync(), Times.Never);
        _ = metadata.IsDirty.Should().BeTrue();
        await InvokeButtonAsync(panel, "Reload").ConfigureAwait(true);
        await InvokeButtonAsync(panel, "Discard changes and reload").ConfigureAwait(true);
        await panel.Pending.ConfigureAwait(true);
        participant.Verify(value => value.ReloadFromDiskAsync(), Times.Once);
        _ = item.IsSaved.Should().BeTrue();
    });

    /// <summary>A pending copy disables duplicate actions and never marks the original saved.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task InlineCopyWaitsForIoAndKeepsTheOriginalUnsaved() => EnqueueAsync(async () =>
    {
        var metadata = new SceneDocumentMetadata(Guid.NewGuid()) { IsDirty = true };
        var completed = new TaskCompletionSource<DocumentConflictResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        var participant = new Mock<IDocumentConflictParticipant>();
        _ = participant.SetupGet(value => value.HasSaveConflict).Returns(value: true);
        _ = participant.SetupGet(value => value.SuggestedCopyName).Returns("Original Copy");
        _ = participant.Setup(value => value.SaveCopyAsync("MyCopy")).Returns(completed.Task);
        var item = new DocumentCloseItem(metadata, () => Task.FromResult(false), participant.Object);
        var panel = new DocumentConflictPanel(item, () => { });
        await LoadTestContentAsync(panel).ConfigureAwait(true);
        await InvokeButtonAsync(panel, "Save Copy").ConfigureAwait(true);
        var name = panel.FindDescendant<TextBox>()!;
        _ = name.Text.Should().Be("Original Copy");
        name.Text = "MyCopy";
        await InvokeButtonAsync(panel, "Save Copy").ConfigureAwait(true);

        _ = panel.IsEnabled.Should().BeFalse();
        _ = item.Pending.IsCompleted.Should().BeFalse();
        _ = item.IsSaved.Should().BeFalse();
        completed.SetResult(new(Succeeded: true, "Copy saved; the original remains unsaved."));
        await panel.Pending.ConfigureAwait(true);
        _ = panel.IsEnabled.Should().BeTrue();
        _ = metadata.IsDirty.Should().BeTrue();
        _ = item.IsSaved.Should().BeFalse();
        participant.Verify(value => value.SaveCopyAsync("MyCopy"), Times.Once);
        _ = panel.FindDescendants().OfType<TextBlock>().Should().Contain(value => string.Equals(value.Text, "Copy saved; the original remains unsaved.", StringComparison.Ordinal));
    });

    private static async Task InvokeButtonAsync(FrameworkElement root, string caption)
    {
        var button = root.FindDescendants().OfType<Button>().Last(value => Equals(value.Content, caption));
        var invoke = (IInvokeProvider)new ButtonAutomationPeer(button).GetPattern(PatternInterface.Invoke);
        invoke.Invoke();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
    }
}
