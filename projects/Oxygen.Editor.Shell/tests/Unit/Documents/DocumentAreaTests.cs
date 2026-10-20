// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.UI;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Shell.Unit.Tests.Documents;

/// <summary>Checks the document area of a window: its tabs, its active document, and the documents it closes.</summary>
[TestClass]
public sealed class DocumentAreaTests
{
    private static readonly WindowId Window = new(1);
    private static readonly WindowId OtherWindow = new(2);

    public TestContext TestContext { get; set; } = null!;

    /// <summary>Showing a document adds its tab once and makes it active; the area announces each change once.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ShowAddsATabOnceAndFollowsTheSelection()
    {
        using var notes = new Notes();
        var tabs = new DocumentTabs();
        using var area = CreateArea(tabs, notes);
        var intro = await notes.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);
        var outro = await notes.OpenAsync("Outro", this.TestContext.CancellationToken).ConfigureAwait(false);
        var changes = 0;
        area.ActiveDocumentChanged += (_, _) => changes++;

        _ = (await area.ShowAsync(intro).ConfigureAwait(false)).Should().BeTrue();
        _ = (await area.ShowAsync(outro).ConfigureAwait(false)).Should().BeTrue();
        _ = (await area.ShowAsync(intro).ConfigureAwait(false)).Should().BeTrue();

        _ = area.Documents.Should().BeEquivalentTo([intro, outro]);
        _ = tabs.GetOpenDocuments(Window).Should().HaveCount(2);
        _ = area.ActiveDocument.Should().BeSameAs(intro);
        _ = changes.Should().Be(3);
    }

    /// <summary>Closing a document's tab closes the document, and leaves no document active.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ClosingTheTabClosesTheDocument()
    {
        using var notes = new Notes();
        var tabs = new DocumentTabs();
        using var area = CreateArea(tabs, notes);
        var intro = await notes.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await area.ShowAsync(intro).ConfigureAwait(false);

        _ = (await area.CloseAsync(intro).ConfigureAwait(false)).Should().BeTrue();

        _ = notes.Documents.Documents.Should().BeEmpty();
        _ = area.Documents.Should().BeEmpty();
        _ = area.ActiveDocument.Should().BeNull();
    }

    /// <summary>Showing a document whose tab is in another window selects it there and brings that window forward.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ShowingADocumentWithATabInAnotherWindowSelectsItThere()
    {
        using var notes = new Notes();
        var tabs = new DocumentTabs();
        var brought = new List<WindowId>();
        using var first = CreateArea(tabs, notes, Window, brought.Add);
        using var second = CreateArea(tabs, notes, OtherWindow, brought.Add);
        var intro = await notes.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);
        var outro = await notes.OpenAsync("Outro", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await first.ShowAsync(intro).ConfigureAwait(false);
        _ = await first.ShowAsync(outro).ConfigureAwait(false);
        brought.Clear();

        _ = (await second.ShowAsync(intro).ConfigureAwait(false)).Should().BeTrue();

        _ = second.Documents.Should().BeEmpty();
        _ = first.ActiveDocument.Should().BeSameAs(intro);
        _ = brought.Should().Equal(Window);
    }

    /// <summary>Preparing the close of several documents closes them together once committed, after their participants.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ASetOfDocumentsClosesTogetherAfterTheirParticipants()
    {
        using var notes = new Notes();
        var coordinator = new DocumentCloseCoordinator(new ApprovingPrompt(), new IgnoredResults());
        var tabs = new DocumentTabs();
        using var coordinatorUse = tabs.UseCloseCoordinator(Window, coordinator);
        using var area = CreateArea(tabs, notes);
        var intro = await notes.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);
        var outro = await notes.OpenAsync("Outro", this.TestContext.CancellationToken).ConfigureAwait(false);
        var kept = await notes.OpenAsync("Kept", this.TestContext.CancellationToken).ConfigureAwait(false);
        Participant[] participants = [new(notes, intro), new(notes, outro)];
        foreach (var participant in participants)
        {
            _ = await area.ShowAsync(participant.Document).ConfigureAwait(false);
            coordinator.Register(tabs.GetOpenDocuments(Window).OfType<DocumentTab>().Single(tab => tab.DocumentId == participant.Document.Id), participant);
        }

        _ = await area.ShowAsync(kept).ConfigureAwait(false);
        using (var transaction = await area.PrepareCloseAsync([intro, outro]).ConfigureAwait(false))
        {
            _ = transaction.Should().NotBeNull();
            _ = notes.Documents.Documents.Should().HaveCount(3, "nothing closes before the commit");
            _ = (await transaction!.CommitAsync().ConfigureAwait(false)).Should().BeTrue();
        }

        _ = participants.Should().OnlyContain(participant => participant.ClosedWhileOpen);
        _ = notes.Documents.Documents.Should().Equal(kept);
        _ = area.Documents.Should().Equal(kept);
    }

    /// <summary>The close of a set that holds a document without a tab here is refused.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ASetWithADocumentNotShownHereIsNotClosed()
    {
        using var notes = new Notes();
        var tabs = new DocumentTabs();
        using var area = CreateArea(tabs, notes);
        var intro = await notes.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);
        var outro = await notes.OpenAsync("Outro", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await area.ShowAsync(intro).ConfigureAwait(false);

        using var transaction = await area.PrepareCloseAsync([intro, outro]).ConfigureAwait(false);

        _ = transaction.Should().BeNull();
        _ = area.Documents.Should().Equal(intro);
    }

    /// <summary>A tab cannot be torn out of its window: the detach is refused and the tab stays.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ATabCannotBeDetached()
    {
        using var notes = new Notes();
        var tabs = new DocumentTabs();
        using var area = CreateArea(tabs, notes);
        var intro = await notes.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await area.ShowAsync(intro).ConfigureAwait(false);

        _ = (await tabs.DetachDocumentAsync(Window, intro.Id).ConfigureAwait(false)).Should().BeNull();

        _ = area.Documents.Should().ContainSingle().Which.Should().BeSameAs(intro);
        _ = area.ActiveDocument.Should().BeSameAs(intro);
    }

    /// <summary>The area refuses to start when a kind has no editor, or an editor names a kind no document opens with.</summary>
    [TestMethod]
    public void EveryKindNeedsExactlyItsEditor()
    {
        using var notes = new Notes();
        var tabs = new DocumentTabs();
        var stranger = new Notes.NoteKind();

        var missing = () => new DocumentArea(tabs, notes.Documents, [], Window, static _ => { });
        var foreign = () => new DocumentArea(tabs, notes.Documents, [Editor(notes.Kind), Editor(stranger)], Window, static _ => { });

        _ = missing.Should().Throw<InvalidOperationException>().WithMessage("*Note kind has no editor*");
        _ = foreign.Should().Throw<InvalidOperationException>().WithMessage("*names a kind*");
    }

    private static DocumentArea CreateArea(DocumentTabs tabs, Notes notes, WindowId? window = null, Action<WindowId>? bringForward = null)
        => new(tabs, notes.Documents, [Editor(notes.Kind)], window ?? Window, bringForward ?? (static _ => { }));

    private static DocumentEditorDescriptor Editor(IDocumentKind kind) => new(kind, static _ => new object());

    // An editor that records whether its document was still open when the close reached it.
    private sealed class Participant(Notes notes, Document document) : IDocumentCloseParticipant
    {
        public Document Document => document;

        public bool ClosedWhileOpen { get; private set; }

        public Task PrepareForCloseAsync() => Task.CompletedTask;

        public Task<bool> SaveForCloseAsync() => Task.FromResult(true);

        public Task CloseAsync(bool discard)
        {
            this.ClosedWhileOpen = notes.Documents.Documents.Contains(document);
            return Task.CompletedTask;
        }

        public void ResumeEditing()
        {
        }
    }

    private sealed class ApprovingPrompt : IDocumentClosePrompt
    {
        public Task<bool> ConfirmAsync(IReadOnlyList<DocumentCloseItem> documents) => Task.FromResult(true);
    }

    private sealed class IgnoredResults : IOperationResultPublisher
    {
        public void Publish(OperationResult result)
        {
        }

        public IDisposable Subscribe(IObserver<OperationResult> observer) => new Unsubscriber();

        private sealed class Unsubscriber : IDisposable
        {
            public void Dispose()
            {
            }
        }
    }
}
