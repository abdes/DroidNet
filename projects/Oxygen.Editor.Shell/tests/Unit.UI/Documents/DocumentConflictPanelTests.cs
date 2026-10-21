// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using DroidNet.TimeMachine;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Unit.Tests.Documents;

namespace Oxygen.Editor.Shell.Unit.UI.Tests.Documents;

/// <summary>Exercises the inline conflict actions of the close prompt and their explicit discard confirmation.</summary>
[TestClass]
[TestCategory("UITest")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository's discovery configuration.")]
public sealed partial class DocumentConflictPanelTests : VisualUserInterfaceTests
{
    /// <summary>Reload requires a second explicit discard action before touching the document.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task InlineReloadRequiresDiscardConfirmation() => EnqueueAsync(async () =>
    {
        using var file = await ConflictedNote.OpenAsync().ConfigureAwait(true);
        var documents = new Mock<IDocumentService>();
        _ = documents.Setup(value => value.ReloadAsync(file.Document)).Returns(async () =>
        {
            _ = await file.Document.ReloadAsync().ConfigureAwait(true);
            return new DocumentConflictResult(Succeeded: true, "Reloaded");
        });
        var item = new DocumentCloseItem(file.Tab, () => Task.FromResult(false), documents.Object);
        var panel = new DocumentConflictPanel(item, () => { });
        await LoadTestContentAsync(panel).ConfigureAwait(true);

        await InvokeButtonAsync(panel, "Reload").ConfigureAwait(true);
        documents.Verify(value => value.ReloadAsync(It.IsAny<Document>()), Times.Never);
        _ = panel.FindDescendants().OfType<TextBlock>().Should().Contain(value => value.Text.Contains("Undo/Redo", StringComparison.Ordinal));
        await InvokeButtonAsync(panel, "Keep Mine").ConfigureAwait(true);
        documents.Verify(value => value.ReloadAsync(It.IsAny<Document>()), Times.Never);
        _ = file.Tab.IsDirty.Should().BeTrue();
        await InvokeButtonAsync(panel, "Reload").ConfigureAwait(true);
        await InvokeButtonAsync(panel, "Reload").ConfigureAwait(true);
        await panel.Pending.ConfigureAwait(true);
        documents.Verify(value => value.ReloadAsync(file.Document), Times.Once);
        _ = item.IsSaved.Should().BeTrue();
    });

    /// <summary>A pending copy disables duplicate actions and never marks the original saved.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task InlineCopyWaitsForIoAndKeepsTheOriginalUnsaved() => EnqueueAsync(async () =>
    {
        using var file = await ConflictedNote.OpenAsync().ConfigureAwait(true);
        var completed = new TaskCompletionSource<DocumentConflictResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        var documents = new Mock<IDocumentService>();
        _ = documents.Setup(value => value.SaveCopyAsync(file.Document, "MyCopy")).Returns(completed.Task);
        var item = new DocumentCloseItem(file.Tab, () => Task.FromResult(false), documents.Object);
        var panel = new DocumentConflictPanel(item, () => { });
        await LoadTestContentAsync(panel).ConfigureAwait(true);
        await InvokeButtonAsync(panel, "Save a Copy").ConfigureAwait(true);
        var name = panel.FindDescendant<TextBox>()!;
        _ = name.Text.Should().Be("Original Copy");
        name.Text = "MyCopy";
        await InvokeButtonAsync(panel, "Save a Copy").ConfigureAwait(true);

        _ = panel.IsEnabled.Should().BeFalse();
        _ = item.Pending.IsCompleted.Should().BeFalse();
        _ = item.IsSaved.Should().BeFalse();
        completed.SetResult(new(Succeeded: true, "Copy saved; the original remains unsaved."));
        await panel.Pending.ConfigureAwait(true);
        _ = panel.IsEnabled.Should().BeTrue();
        _ = file.Tab.IsDirty.Should().BeTrue();
        _ = item.IsSaved.Should().BeFalse();
        documents.Verify(value => value.SaveCopyAsync(file.Document, "MyCopy"), Times.Once);
        _ = panel.FindDescendants().OfType<TextBlock>().Should().Contain(value => string.Equals(value.Text, "Copy saved; the original remains unsaved.", StringComparison.Ordinal));
    });

    private static async Task InvokeButtonAsync(FrameworkElement root, string caption)
    {
        var button = root.FindDescendants().OfType<Button>().Last(value => Equals(value.Content, caption) && value.Visibility == Visibility.Visible && IsShown(value));
        var invoke = (IInvokeProvider)new ButtonAutomationPeer(button).GetPattern(PatternInterface.Invoke);
        invoke.Invoke();
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
    }

    // A button counts as shown when no panel above it is collapsed.
    private static bool IsShown(FrameworkElement element)
    {
        for (var current = element.Parent as FrameworkElement; current is not null; current = current.Parent as FrameworkElement)
        {
            if (current.Visibility != Visibility.Visible)
            {
                return false;
            }
        }

        return true;
    }

    /// <summary>A note document with an unsaved edit whose file another program changed.</summary>
    private sealed partial class ConflictedNote : IDisposable
    {
        private readonly Notes notes;

        private ConflictedNote(Notes notes, Document document)
        {
            this.notes = notes;
            this.Document = document;
            this.Tab = new DocumentTab(document, new DocumentEditorDescriptor(notes.Kind, static _ => new object()));
        }

        public Document Document { get; }

        public DocumentTab Tab { get; }

        public static async Task<ConflictedNote> OpenAsync()
        {
            var notes = new Notes();
            var document = await notes.OpenAsync("Original", CancellationToken.None).ConfigureAwait(true);
            document.History.AddChange("Edit", static () => { });
            await notes.WriteAsync("Original", "changed", CancellationToken.None).ConfigureAwait(true);
            _ = await document.CheckFileAsync().ConfigureAwait(true);
            return new ConflictedNote(notes, document);
        }

        public void Dispose()
        {
            this.Tab.Dispose();
            this.notes.Dispose();
        }
    }
}
