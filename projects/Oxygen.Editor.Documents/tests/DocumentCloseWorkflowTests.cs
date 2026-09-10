// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Documents;
using Microsoft.UI;
using Moq;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Documents.Tests;

/// <summary>Exercises real document lifecycle entry points with authoring and dialog boundaries.</summary>
[TestClass]
public sealed class DocumentCloseWorkflowTests
{
    private static readonly WindowId Window = new(1);

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task CancelTabOrSceneReplacementPreservesDocumentAndActiveContext(bool replaceScene)
    {
        var fixture = new Fixture((_, _) => Task.FromResult(false));
        var original = await fixture.OpenAsync("Original", closable: !replaceScene).ConfigureAwait(false);
        var closedEvents = 0;
        fixture.Service.DocumentClosed += (_, _) => closedEvents++;

        if (replaceScene)
        {
            var replacement = new TestDocumentMetadata { IsClosable = false, Title = "Replacement" };
            var result = await fixture.Service.OpenDocumentAsync(Window, replacement).ConfigureAwait(false);
            _ = result.Should().Be(Guid.Empty);
        }
        else
        {
            _ = (await fixture.Service.CloseDocumentAsync(Window, original.Metadata.DocumentId).ConfigureAwait(false)).Should().BeFalse();
        }

        _ = fixture.Service.GetOpenDocuments(Window).Should().ContainSingle().Which.Should().BeSameAs(original.Metadata);
        _ = fixture.Service.GetActiveDocumentId(Window).Should().Be(original.Metadata.DocumentId);
        _ = original.Metadata.IsDirty.Should().BeTrue();
        _ = original.History.Should().Equal("edit");
        _ = original.CloseCalls.Should().Be(0);
        _ = original.IsPreparing.Should().BeFalse();
        _ = closedEvents.Should().Be(0);
    }

    [TestMethod]
    public async Task SceneReplacementSavesBeforeReleasingOriginal()
    {
        var fixture = new Fixture(async (items, workspace) =>
        {
            _ = workspace.Should().BeFalse();
            return await items.Single().SaveAsync().ConfigureAwait(false);
        });
        var original = await fixture.OpenAsync("Scene A", closable: false).ConfigureAwait(false);
        var replacement = new TestDocumentMetadata { IsClosable = false, Title = "Scene B" };

        _ = (await fixture.Service.OpenDocumentAsync(Window, replacement).ConfigureAwait(false)).Should().Be(replacement.DocumentId);

        _ = original.SavedValue.Should().Be("edit");
        _ = original.Discarded.Should().BeFalse();
        _ = original.CloseCalls.Should().Be(1);
        _ = fixture.Service.GetActiveDocumentId(Window).Should().Be(replacement.DocumentId);
    }

    [TestMethod]
    public async Task WorkspaceSavesSelectedDocumentsBeforeAnyDiscardOrClose()
    {
        var fixture = new Fixture(async (items, workspace) =>
        {
            _ = workspace.Should().BeTrue();
            _ = items.Should().OnlyContain(item => item.IsSelected);
            items[1].IsSelected = false;
            return await items[0].SaveAsync().ConfigureAwait(false);
        });
        var saved = await fixture.OpenAsync("Save me").ConfigureAwait(false);
        var discarded = await fixture.OpenAsync("Discard me").ConfigureAwait(false);

        using var transaction = await fixture.Service.PrepareCloseAllAsync(Window).ConfigureAwait(false);
        _ = transaction.Should().NotBeNull();
        _ = fixture.Service.GetOpenDocuments(Window).Should().HaveCount(2);
        _ = discarded.History.Should().Equal("edit");
        _ = saved.CloseCalls.Should().Be(0);
        _ = discarded.CloseCalls.Should().Be(0);

        _ = (await transaction!.CommitAsync().ConfigureAwait(false)).Should().BeTrue();

        _ = saved.SavedValue.Should().Be("edit");
        _ = saved.Discarded.Should().BeFalse();
        _ = discarded.Discarded.Should().BeTrue();
        _ = fixture.Service.GetOpenDocuments(Window).Should().BeEmpty();
    }

    [TestMethod]
    public async Task SaveFailureThenCancelDiscardsNothingAndKeepsSuccessfulSaves()
    {
        var fixture = new Fixture(async (items, workspace) =>
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
        _ = saved.Metadata.IsDirty.Should().BeFalse();
        _ = failed.Metadata.IsDirty.Should().BeTrue();
        _ = uncheckedDocument.History.Should().Equal("edit");
        _ = fixture.Participants.Should().OnlyContain(participant => participant.CloseCalls == 0 && !participant.IsPreparing);
        fixture.Results.Verify(
            publisher => publisher.Publish(It.Is<OperationResult>(result =>
                result.Status == OperationStatus.Failed && result.AffectedScope.DocumentId == failed.Metadata.DocumentId)),
            Times.Once);
    }

    [TestMethod]
    public async Task FailedSaveCanBeRetriedBeforeClose()
    {
        var fixture = new Fixture(async (items, workspace) =>
        {
            _ = (await items[0].SaveAsync().ConfigureAwait(false)).Should().BeFalse();
            return await items[0].SaveAsync().ConfigureAwait(false);
        });
        var document = await fixture.OpenAsync("Retry").ConfigureAwait(false);
        var attempt = 0;
        document.Save = () => Task.FromResult(++attempt == 2);

        _ = (await fixture.Service.CloseDocumentAsync(Window, document.Metadata.DocumentId).ConfigureAwait(false)).Should().BeTrue();
        _ = attempt.Should().Be(2);
        _ = document.Discarded.Should().BeFalse();
    }

    [TestMethod]
    public async Task LaterDocumentVetoPreventsWorkspacePromptAndAllTeardown()
    {
        var promptCalled = false;
        var fixture = new Fixture((_, _) =>
        {
            promptCalled = true;
            return Task.FromResult(false);
        });
        var first = await fixture.OpenAsync("First").ConfigureAwait(false);
        var vetoed = await fixture.OpenAsync("Vetoed").ConfigureAwait(false);
        fixture.Service.DocumentClosing += (_, args) =>
        {
            if (args.Metadata.DocumentId == vetoed.Metadata.DocumentId)
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
        var fixture = new Fixture((items, workspace) =>
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
        _ = document.Metadata.IsDirty.Should().BeTrue();
        _ = document.History.Should().Equal("edit");
        _ = document.CloseCalls.Should().Be(0);
    }

    [TestMethod]
    public async Task PendingDecisionRejectsOverlappingCloseOpenAndActivation()
    {
        var answer = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        var fixture = new Fixture((_, _) => answer.Task);
        var document = await fixture.OpenAsync("Pending").ConfigureAwait(false);
        var close = fixture.Service.CloseDocumentAsync(Window, document.Metadata.DocumentId);

        _ = (await fixture.Service.CloseDocumentAsync(Window, document.Metadata.DocumentId).ConfigureAwait(false)).Should().BeFalse();
        _ = (await fixture.Service.OpenDocumentAsync(Window, new TestDocumentMetadata()).ConfigureAwait(false)).Should().Be(Guid.Empty);
        _ = (await fixture.Service.SelectDocumentAsync(Window, document.Metadata.DocumentId).ConfigureAwait(false)).Should().BeFalse();
        answer.SetResult(false);
        _ = (await close.ConfigureAwait(false)).Should().BeFalse();
        _ = (await fixture.Service.SelectDocumentAsync(Window, document.Metadata.DocumentId).ConfigureAwait(false)).Should().BeTrue();
    }

    [TestMethod]
    public async Task EditArrivingDuringSaveCannotBeMarkedSavedAndClosed()
    {
        var fixture = new Fixture(async (items, workspace) => await items[0].SaveAsync().ConfigureAwait(false));
        var document = await fixture.OpenAsync("Newer edits").ConfigureAwait(false);
        document.Save = () =>
        {
            document.Metadata.IsDirty = true;
            return Task.FromResult(true);
        };

        _ = (await fixture.Service.CloseDocumentAsync(Window, document.Metadata.DocumentId).ConfigureAwait(false)).Should().BeFalse();
        _ = document.Metadata.IsDirty.Should().BeTrue();
        _ = document.CloseCalls.Should().Be(0);
        fixture.Results.Verify(publisher => publisher.Publish(It.IsAny<OperationResult>()), Times.Once);
    }

    private sealed class Fixture
    {
        private readonly DocumentCloseCoordinator coordinator;

        public Fixture(Func<IReadOnlyList<DocumentCloseItem>, bool, Task<bool>> prompt)
        {
            this.coordinator = new DocumentCloseCoordinator(new Prompt(prompt), this.Results.Object);
            this.Service = new EditorDocumentService(closeCoordinator: this.coordinator);
        }

        public Mock<IOperationResultPublisher> Results { get; } = new();

        public EditorDocumentService Service { get; }

        public List<Participant> Participants { get; } = [];

        public async Task<Participant> OpenAsync(string title, bool closable = true)
        {
            var metadata = new TestDocumentMetadata { Title = title, IsClosable = closable, IsDirty = true };
            var participant = new Participant(metadata);
            _ = (await this.Service.OpenDocumentAsync(Window, metadata).ConfigureAwait(false)).Should().Be(metadata.DocumentId);
            this.coordinator.Register(Window, metadata.DocumentId, participant);
            this.Participants.Add(participant);
            return participant;
        }
    }

    private sealed class Prompt(Func<IReadOnlyList<DocumentCloseItem>, bool, Task<bool>> show) : IDocumentClosePrompt
    {
        public Task<bool> ConfirmAsync(WindowId windowId, IReadOnlyList<DocumentCloseItem> documents, bool isWorkspaceClose)
            => show(documents, isWorkspaceClose);
    }

    private sealed class Participant(TestDocumentMetadata metadata) : IDocumentCloseParticipant
    {
        public TestDocumentMetadata Metadata { get; } = metadata;

        public List<string> History { get; } = ["edit"];

        public string? SavedValue { get; private set; }

        public int CloseCalls { get; private set; }

        public bool Discarded { get; private set; }

        public bool IsPreparing { get; private set; }

        public Func<Task<bool>> Save { get; set; } = () => Task.FromResult(true);

        public Task PrepareForCloseAsync()
        {
            this.IsPreparing = true;
            return Task.CompletedTask;
        }

        public async Task<bool> SaveForCloseAsync()
        {
            if (!await this.Save().ConfigureAwait(false))
            {
                return false;
            }

            this.SavedValue = string.Join(',', this.History);
            this.Metadata.IsDirty = false;
            return true;
        }

        public Task CloseAsync(bool discard)
        {
            this.CloseCalls++;
            this.Discarded = discard;
            this.History.Clear();
            return Task.CompletedTask;
        }

        public void ResumeEditing() => this.IsPreparing = false;
    }
}
