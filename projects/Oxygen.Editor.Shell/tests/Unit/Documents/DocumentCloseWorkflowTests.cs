// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.TimeMachine;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Shell.Unit.Tests.Documents;

/// <summary>Exercises the close of document tabs through each window's close coordinator, with stand-in editors and prompt.</summary>
[TestClass]
public sealed partial class DocumentCloseWorkflowTests
{
    private static readonly WindowId Window = new(1);

    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task CancelTabClosePreservesDocumentAndActiveContext()
    {
        using var fixture = new Fixture(_ => Task.FromResult(false));
        var original = await fixture.OpenAsync("Original").ConfigureAwait(false);
        var closedEvents = 0;
        fixture.Service.DocumentClosed += (_, _) => closedEvents++;

        _ = (await fixture.Service.CloseDocumentAsync(Window, original.Tab.DocumentId).ConfigureAwait(false)).Should().BeFalse();

        _ = fixture.Service.GetOpenDocuments(Window).Should().ContainSingle().Which.Should().BeSameAs(original.Tab);
        _ = fixture.Service.GetActiveDocumentId(Window).Should().Be(original.Tab.DocumentId);
        _ = original.Tab.IsDirty.Should().BeTrue();
        _ = original.CloseCalls.Should().Be(0);
        _ = original.IsPreparing.Should().BeFalse();
        _ = closedEvents.Should().Be(0);
    }

    [TestMethod]
    public async Task WorkspaceSavesSelectedDocumentsBeforeAnyDiscardOrClose()
    {
        using var fixture = new Fixture(async items =>
        {
            _ = items.Should().OnlyContain(item => item.IsSelected);
            items[1].IsSelected = false;
            return await items[0].SaveAsync().ConfigureAwait(false);
        });
        var saved = await fixture.OpenAsync("Save me").ConfigureAwait(false);
        var discarded = await fixture.OpenAsync("Discard me").ConfigureAwait(false);

        using var transaction = await fixture.Service.PrepareCloseAllAsync(Window).ConfigureAwait(false);
        _ = transaction.Should().NotBeNull();
        _ = fixture.Service.GetOpenDocuments(Window).Should().HaveCount(2);
        _ = discarded.Tab.IsDirty.Should().BeTrue();
        _ = saved.CloseCalls.Should().Be(0);
        _ = discarded.CloseCalls.Should().Be(0);

        _ = (await transaction!.CommitAsync().ConfigureAwait(false)).Should().BeTrue();

        _ = saved.Saved.Should().BeTrue();
        _ = saved.Discarded.Should().BeFalse();
        _ = discarded.Discarded.Should().BeTrue();
        _ = fixture.Service.GetOpenDocuments(Window).Should().BeEmpty();
    }

    [TestMethod]
    public async Task SaveFailureThenCancelDiscardsNothingAndKeepsSuccessfulSaves()
    {
        using var fixture = new Fixture(async items =>
        {
            _ = (await items[0].SaveAsync().ConfigureAwait(false)).Should().BeTrue();
            _ = (await items[1].SaveAsync().ConfigureAwait(false)).Should().BeFalse();
            items[2].IsSelected = false;
            return false;
        });
        var saved = await fixture.OpenAsync("Saved").ConfigureAwait(false);
        var failed = await fixture.OpenAsync("Failed").ConfigureAwait(false);
        failed.Save = () => throw new IOException("Disk unavailable");
        var uncheckedDocument = await fixture.OpenAsync("Unchecked").ConfigureAwait(false);
        var activeId = fixture.Service.GetActiveDocumentId(Window);

        using var transaction = await fixture.Service.PrepareCloseAllAsync(Window).ConfigureAwait(false);

        _ = transaction.Should().BeNull();
        _ = fixture.Service.GetOpenDocuments(Window).Should().HaveCount(3);
        _ = fixture.Service.GetActiveDocumentId(Window).Should().Be(activeId);
        _ = saved.Tab.IsDirty.Should().BeFalse();
        _ = failed.Tab.IsDirty.Should().BeTrue();
        _ = uncheckedDocument.Tab.IsDirty.Should().BeTrue();
        _ = fixture.Participants.Should().OnlyContain(participant => participant.CloseCalls == 0 && !participant.IsPreparing);
        fixture.Results.Verify(
            publisher => publisher.Publish(It.Is<OperationResult>(result =>
                result.Status == OperationStatus.Failed)),
            Times.Once);
    }

    [TestMethod]
    public async Task FailedSaveCanBeRetriedBeforeClose()
    {
        using var fixture = new Fixture(async items =>
        {
            _ = (await items[0].SaveAsync().ConfigureAwait(false)).Should().BeFalse();
            return await items[0].SaveAsync().ConfigureAwait(false);
        });
        var document = await fixture.OpenAsync("Retry").ConfigureAwait(false);
        var attempt = 0;
        document.Save = async () => ++attempt == 2 && (await document.Tab.Document.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded;

        _ = (await fixture.Service.CloseDocumentAsync(Window, document.Tab.DocumentId).ConfigureAwait(false)).Should().BeTrue();
        _ = attempt.Should().Be(2);
        _ = document.Discarded.Should().BeFalse();
    }

    [TestMethod]
    public async Task LaterDocumentVetoPreventsWorkspacePromptAndAllTeardown()
    {
        var promptCalled = false;
        using var fixture = new Fixture(_ =>
        {
            promptCalled = true;
            return Task.FromResult(false);
        });
        var first = await fixture.OpenAsync("First").ConfigureAwait(false);
        var vetoed = await fixture.OpenAsync("Vetoed").ConfigureAwait(false);
        fixture.Service.DocumentClosing += (_, args) =>
        {
            if (args.Metadata.DocumentId == vetoed.Tab.DocumentId)
            {
                args.AddVetoTask(Task.FromResult(false));
            }
        };

        using var transaction = await fixture.Service.PrepareCloseAllAsync(Window).ConfigureAwait(false);

        _ = transaction.Should().BeNull();
        _ = first.CloseCalls.Should().Be(0);
        _ = promptCalled.Should().BeFalse();
        _ = fixture.Service.GetOpenDocuments(Window).Should().HaveCount(2);
    }

    [TestMethod]
    public async Task AbandonedPreparedWorkspaceClosePreservesUncheckedDocuments()
    {
        using var fixture = new Fixture(items =>
        {
            foreach (var item in items)
            {
                item.IsSelected = false;
            }

            return Task.FromResult(true);
        });
        var document = await fixture.OpenAsync("Keep me").ConfigureAwait(false);
        using (var transaction = await fixture.Service.PrepareCloseAllAsync(Window).ConfigureAwait(false))
        {
            _ = transaction.Should().NotBeNull();
            _ = document.IsPreparing.Should().BeTrue();

            // A later window guard cancels; the transaction must be disposed without committing.
        }

        _ = document.IsPreparing.Should().BeFalse();
        _ = document.Tab.IsDirty.Should().BeTrue();
        _ = document.CloseCalls.Should().Be(0);
    }

    [TestMethod]
    public async Task PendingDecisionRejectsOverlappingCloseOpenAndActivation()
    {
        var answer = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        using var fixture = new Fixture(_ => answer.Task);
        var document = await fixture.OpenAsync("Pending").ConfigureAwait(false);
        var close = fixture.Service.CloseDocumentAsync(Window, document.Tab.DocumentId);

        _ = (await fixture.Service.CloseDocumentAsync(Window, document.Tab.DocumentId).ConfigureAwait(false)).Should().BeFalse();
        _ = (await fixture.Service.OpenDocumentAsync(Window, new TestDocumentMetadata()).ConfigureAwait(false)).Should().BeFalse();
        _ = (await fixture.Service.SelectDocumentAsync(Window, document.Tab.DocumentId).ConfigureAwait(false)).Should().BeFalse();
        answer.SetResult(false);
        _ = (await close.ConfigureAwait(false)).Should().BeFalse();
        _ = (await fixture.Service.SelectDocumentAsync(Window, document.Tab.DocumentId).ConfigureAwait(false)).Should().BeTrue();
    }

    [TestMethod]
    public async Task EditArrivingDuringSaveCannotBeMarkedSavedAndClosed()
    {
        using var fixture = new Fixture(async items => await items[0].SaveAsync().ConfigureAwait(false));
        var document = await fixture.OpenAsync("Newer edits").ConfigureAwait(false);
        document.Save = async () =>
        {
            var saved = (await document.Tab.Document.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded;
            document.Tab.Document.History.AddChange("Newer", static () => { });
            return saved;
        };

        _ = (await fixture.Service.CloseDocumentAsync(Window, document.Tab.DocumentId).ConfigureAwait(false)).Should().BeFalse();
        _ = document.Tab.IsDirty.Should().BeTrue();
        _ = document.CloseCalls.Should().Be(0);
        fixture.Results.Verify(publisher => publisher.Publish(It.IsAny<OperationResult>()), Times.Once);
    }

    [TestMethod]
    public async Task ProjectReplacement_LaterWindowVetoPreservesEveryWindow()
    {
        var promptCount = 0;
        using var fixture = new Fixture(items =>
        {
            foreach (var item in items)
            {
                item.IsSelected = false;
            }

            return Task.FromResult(++promptCount == 1);
        });
        var otherWindow = new WindowId(2);
        var first = await fixture.OpenAsync("First").ConfigureAwait(false);
        var second = await fixture.OpenAsync("Second", window: otherWindow).ConfigureAwait(false);

        using var transaction = await fixture.Service.PrepareCloseAllWindowsAsync().ConfigureAwait(false);

        _ = transaction.Should().BeNull();
        _ = fixture.Service.GetOpenDocuments(Window).Should().ContainSingle().Which.Should().BeSameAs(first.Tab);
        _ = fixture.Service.GetOpenDocuments(otherWindow).Should().ContainSingle().Which.Should().BeSameAs(second.Tab);
        _ = fixture.Participants.Should().OnlyContain(participant => participant.CloseCalls == 0 && !participant.IsPreparing);
    }

    [TestMethod]
    public async Task ProjectReplacement_AllWindowsRemainOpenUntilApprovedCommit()
    {
        using var fixture = new Fixture(items =>
        {
            foreach (var item in items)
            {
                item.IsSelected = false;
            }

            return Task.FromResult(true);
        });
        var otherWindow = new WindowId(2);
        var first = await fixture.OpenAsync("First").ConfigureAwait(false);
        var second = await fixture.OpenAsync("Second", window: otherWindow).ConfigureAwait(false);
        using var transaction = await fixture.Service.PrepareCloseAllWindowsAsync().ConfigureAwait(false);

        _ = transaction.Should().NotBeNull();
        _ = first.CloseCalls.Should().Be(0);
        _ = second.CloseCalls.Should().Be(0);
        _ = (await transaction!.CommitAsync().ConfigureAwait(false)).Should().BeTrue();
        _ = fixture.Service.GetOpenDocuments(Window).Should().BeEmpty();
        _ = fixture.Service.GetOpenDocuments(otherWindow).Should().BeEmpty();
        _ = fixture.Participants.Should().OnlyContain(participant => participant.CloseCalls == 1);
    }

    /// <summary>Open note documents with an unsaved edit each, shown in the tabs of one or two windows.</summary>
    private sealed partial class Fixture(Func<IReadOnlyList<DocumentCloseItem>, Task<bool>> prompt) : IDisposable
    {
        private readonly Notes notes = new();
        private readonly Dictionary<WindowId, DocumentCloseCoordinator> coordinators = [];
        private readonly List<IDisposable> coordinatorUses = [];
        private readonly List<DocumentTab> tabs = [];

        public Mock<IOperationResultPublisher> Results { get; } = new();

        public DocumentTabs Service { get; } = new();

        public List<Participant> Participants { get; } = [];

        public async Task<Participant> OpenAsync(string title, bool closable = true, WindowId? window = null)
        {
            var target = window ?? Window;
            var document = await this.notes.OpenAsync(title, CancellationToken.None).ConfigureAwait(false);
            document.History.AddChange("Edit", static () => { });
            var tab = new DocumentTab(document, new DocumentEditorDescriptor(this.notes.Kind, static _ => new object()) { IsClosable = closable });
            this.tabs.Add(tab);
            var participant = new Participant(tab);
            _ = (await this.Service.OpenDocumentAsync(target, tab).ConfigureAwait(false)).Should().BeTrue();
            this.CoordinatorOf(target).Register(tab, participant);
            this.Participants.Add(participant);
            return participant;
        }

        public void Dispose()
        {
            this.coordinatorUses.ForEach(static use => use.Dispose());
            this.tabs.ForEach(static tab => tab.Dispose());
            this.notes.Dispose();
        }

        private DocumentCloseCoordinator CoordinatorOf(WindowId window)
        {
            if (!this.coordinators.TryGetValue(window, out var coordinator))
            {
                coordinator = new DocumentCloseCoordinator(new Prompt(prompt), this.Results.Object);
                this.coordinators.Add(window, coordinator);
                this.coordinatorUses.Add(this.Service.UseCloseCoordinator(window, coordinator));
            }

            return coordinator;
        }
    }

    private sealed class Prompt(Func<IReadOnlyList<DocumentCloseItem>, Task<bool>> show) : IDocumentClosePrompt
    {
        public Task<bool> ConfirmAsync(IReadOnlyList<DocumentCloseItem> documents) => show(documents);
    }

    /// <summary>Stands in for a document's editor at close: it saves the document, or discards its edits.</summary>
    private sealed class Participant(DocumentTab tab) : IDocumentCloseParticipant
    {
        public DocumentTab Tab { get; } = tab;

        public bool Saved { get; private set; }

        public int CloseCalls { get; private set; }

        public bool Discarded { get; private set; }

        public bool IsPreparing { get; private set; }

        public Func<Task<bool>> Save { get; set; } = async () => (await tab.Document.SaveAsync().ConfigureAwait(false)).Succeeded;

        public Task PrepareForCloseAsync()
        {
            this.IsPreparing = true;
            return Task.CompletedTask;
        }

        public async Task<bool> SaveForCloseAsync()
        {
            this.Saved = await this.Save().ConfigureAwait(false);
            return this.Saved;
        }

        public Task CloseAsync(bool discard)
        {
            this.CloseCalls++;
            this.Discarded = discard;
            return Task.CompletedTask;
        }

        public void ResumeEditing() => this.IsPreparing = false;
    }
}
