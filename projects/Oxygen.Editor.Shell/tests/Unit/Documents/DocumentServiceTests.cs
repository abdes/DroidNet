// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using DroidNet.Documents;
using DroidNet.TimeMachine;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Shell.Unit.Tests.Documents;

/// <summary>
/// The document service of a window's area: it opens a file once, places its document, saves through editors, and keeps
/// documents in step with their files.
/// </summary>
[TestClass]
public sealed class DocumentServiceTests
{
    private static readonly WindowId Window = new(5);

    public TestContext TestContext { get; set; } = null!;

    /// <summary>Opening an asset twice shows its one document, whose editor the kind's descriptor made once.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AnOpenAssetIsShownWithTheEditorItsKindMade()
    {
        using var fixture = new Fixture();
        await fixture.Notes.WriteAsync("Intro", "Intro", this.TestContext.CancellationToken).ConfigureAwait(false);

        var first = await fixture.Service.OpenAsync(Notes.AssetOf("Intro"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var second = await fixture.Service.OpenAsync(Notes.AssetOf("Intro"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = first.Succeeded.Should().BeTrue(first.Problem);
        _ = second.Document.Should().BeSameAs(first.Document);
        _ = fixture.Notes.Documents.Documents.Should().ContainSingle();
        _ = fixture.Editors.Should().ContainSingle();
        _ = fixture.Service.ActiveEditor.Should().BeSameAs(fixture.Editors[0]);
        _ = fixture.Service.EditorOf(first.Document!).Should().BeSameAs(fixture.Editors[0]);
    }

    /// <summary>A kind with one document per area closes the shown document before it shows the next one.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AKindWithOneDocumentPerAreaReplacesTheShownOne()
    {
        using var fixture = new Fixture(onePerArea: true);
        var intro = await fixture.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);
        await fixture.Notes.WriteAsync("Outro", "Outro", this.TestContext.CancellationToken).ConfigureAwait(false);

        var placement = await fixture.Service.OpenAsync(Notes.AssetOf("Outro"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = placement.Succeeded.Should().BeTrue(placement.Problem);
        _ = placement.Replaced.Should().Equal(intro.File.Asset);
        _ = fixture.Area.Documents.Should().Equal(placement.Document!);
        _ = fixture.Notes.Documents.Documents.Should().Equal(placement.Document!);
        _ = fixture.Editors[0].Disposed.Should().BeTrue();
    }

    /// <summary>Cancelling the question about the shown document's changes opens nothing and keeps it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ACancelledReplacementKeepsTheShownDocument()
    {
        using var fixture = new Fixture(onePerArea: true, approveClose: false);
        var intro = await fixture.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);
        intro.History.AddChange("Edit", static () => { });
        await fixture.Notes.WriteAsync("Outro", "Outro", this.TestContext.CancellationToken).ConfigureAwait(false);

        var placement = await fixture.Service.OpenAsync(Notes.AssetOf("Outro"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = placement.Succeeded.Should().BeFalse();
        _ = placement.Problem.Should().BeNull("the user cancelled");
        _ = fixture.Area.Documents.Should().Equal(intro);
        _ = intro.IsDirty.Should().BeTrue();
        fixture.Results.Verify(value => value.Publish(It.IsAny<OperationResult>()), Times.Never);
    }

    /// <summary>A file that does not open keeps the shown document and publishes why.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AFileThatDoesNotOpenKeepsTheShownDocument()
    {
        using var fixture = new Fixture(onePerArea: true);
        var intro = await fixture.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);

        var placement = await fixture.Service.OpenAsync(Notes.AssetOf("Missing"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = placement.Succeeded.Should().BeFalse();
        _ = placement.Problem.Should().NotBeNull();
        _ = placement.Replaced.Should().BeEmpty();
        _ = fixture.Area.Documents.Should().Equal(intro);
        fixture.Results.Verify(
            value => value.Publish(It.Is<OperationResult>(result => result.Diagnostics.Any(diagnostic => diagnostic.Code == DiagnosticCodes.DocumentPrefix + "OPEN_FAILED"))),
            Times.Once);
    }

    /// <summary>Saving by asset saves the open documents of those assets through their editors, and skips the others.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task SavingByAssetSavesThroughTheEditors()
    {
        using var fixture = new Fixture();
        var intro = await fixture.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);
        intro.History.AddChange("Edit", static () => { });

        var saved = await fixture.Service.SaveAsync([intro.File.Asset, Notes.AssetOf("Closed")]).ConfigureAwait(false);

        _ = saved.Should().BeTrue();
        _ = intro.IsDirty.Should().BeFalse();
        _ = fixture.Editors.Should().ContainSingle().Which.Saves.Should().Be(1);
    }

    /// <summary>A relocation moves the documents it maps, and a delete in the editor closes the deleted file's document.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task DocumentsFollowRelocationsAndCloseWhenDeleted()
    {
        var relocations = new FakeRelocations();
        using var fixture = new Fixture(relocations: relocations);
        var intro = await fixture.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);
        var outro = await fixture.OpenAsync("Outro", this.TestContext.CancellationToken).ConfigureAwait(false);
        var moved = new DocumentFile(Notes.PathOf("Moved"), Notes.AssetOf("Moved"));

        await relocations.RelocateAsync(document => ReferenceEquals(document, intro)
            ? new DocumentRelocation(moved, document.Version, static _ => null)
            : null).ConfigureAwait(false);
        relocations.Delete(outro.File.Path);

        _ = intro.File.Should().Be(moved);
        _ = fixture.Area.Documents.Should().Equal(intro);
        _ = fixture.Notes.Documents.Documents.Should().Equal(intro);
    }

    /// <summary>
    /// A change on disk reloads a clean document, and marks an edited one changed on disk, which Keep Mine resolves. The
    /// in-memory files answer at once, so each check completes before the change is handed on.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task OutsideChangesReloadCleanDocumentsAndMarkEditedOnes()
    {
        using var fixture = new Fixture();
        var clean = await fixture.OpenAsync("Clean", this.TestContext.CancellationToken).ConfigureAwait(false);
        var edited = await fixture.OpenAsync("Edited", this.TestContext.CancellationToken).ConfigureAwait(false);
        edited.History.AddChange("Edit", static () => { });
        var reloads = 0;
        clean.ContentReplaced += (_, _) => reloads++;

        await fixture.Notes.WriteAsync("Clean", "changed", this.TestContext.CancellationToken).ConfigureAwait(false);
        await fixture.Notes.WriteAsync("Edited", "changed", this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.Changes.OnNext(new AssetChange(AssetChangeKind.Updated, clean.File.Asset));
        fixture.Changes.OnNext(new AssetChange(AssetChangeKind.Updated, edited.File.Asset));

        _ = reloads.Should().Be(1);
        _ = edited.IsChangedOnDisk.Should().BeTrue();
        _ = edited.IsDirty.Should().BeTrue();

        await fixture.Service.KeepMineAsync(edited).ConfigureAwait(false);
        _ = edited.IsChangedOnDisk.Should().BeFalse();
        _ = edited.IsDirty.Should().BeTrue();
    }

    /// <summary>Saving a copy writes a new file beside the document and leaves the document unsaved.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ACopyIsSavedBesideTheDocumentWhichStaysUnsaved()
    {
        using var fixture = new Fixture();
        var intro = await fixture.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);
        intro.History.AddChange("Edit", static () => { });

        var result = await fixture.Service.SaveCopyAsync(intro, "IntroCopy").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue(result.Message);
        _ = (await fixture.Notes.Files.ReadAsync(Notes.PathOf("IntroCopy").FullPath, this.TestContext.CancellationToken).ConfigureAwait(false)).Version.Exists.Should().BeTrue();
        _ = intro.IsDirty.Should().BeTrue();
        fixture.Committer.Verify(value => value.CommitAsync(), Times.Once);
    }

    /// <summary>With nothing unsaved, an operation that reads saved files goes on without asking.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task SaveBeforeWithNothingUnsavedContinuesWithoutAsking()
    {
        using var fixture = new Fixture();
        _ = await fixture.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);

        var proceed = await fixture.Service.SaveBeforeAsync(SaveBeforeOperation.Play, static _ => Task.FromResult<IReadOnlyList<UnsavedDocument>>([]), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = proceed.Should().BeTrue();
        fixture.Confirmations.Verify(value => value.ConfirmSaveBeforeAsync(It.IsAny<IReadOnlyList<UnsavedDocument>>(), It.IsAny<SaveBeforeOperation>()), Times.Never);
    }

    /// <summary>Cancelling the question stops the operation and leaves the documents unsaved.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task SaveBeforeCancelledStopsTheOperationAndKeepsTheChanges()
    {
        using var fixture = new Fixture();
        var intro = await fixture.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);
        intro.History.AddChange("Edit", static () => { });
        _ = fixture.Confirmations.Setup(value => value.ConfirmSaveBeforeAsync(It.IsAny<IReadOnlyList<UnsavedDocument>>(), SaveBeforeOperation.Cook)).ReturnsAsync(value: false);

        var proceed = await fixture.Service.SaveBeforeAsync(SaveBeforeOperation.Cook, _ => Task.FromResult<IReadOnlyList<UnsavedDocument>>([Describe(intro)]), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = proceed.Should().BeFalse();
        _ = intro.IsDirty.Should().BeTrue();
        _ = fixture.Editors.Should().ContainSingle().Which.Saves.Should().Be(0);
    }

    /// <summary>Confirming the question saves the documents it listed and lets the operation go on.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task SaveBeforeConfirmedSavesTheListedDocuments()
    {
        using var fixture = new Fixture();
        var intro = await fixture.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);
        var outro = await fixture.OpenAsync("Outro", this.TestContext.CancellationToken).ConfigureAwait(false);
        intro.History.AddChange("Edit", static () => { });
        outro.History.AddChange("Edit", static () => { });
        _ = fixture.Confirmations.Setup(value => value.ConfirmSaveBeforeAsync(It.IsAny<IReadOnlyList<UnsavedDocument>>(), SaveBeforeOperation.Play)).ReturnsAsync(value: true);

        var proceed = await fixture.Service.SaveBeforeAsync(SaveBeforeOperation.Play, _ => Task.FromResult<IReadOnlyList<UnsavedDocument>>([Describe(intro)]), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = proceed.Should().BeTrue();
        _ = intro.IsDirty.Should().BeFalse();
        _ = outro.IsDirty.Should().BeTrue("the operation did not need it");
    }

    /// <summary>The operation looks for unsaved documents after the edits in progress were finished.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task SaveBeforeFinishesEditsInProgressBeforeLookingForUnsavedDocuments()
    {
        using var fixture = new Fixture();
        var intro = await fixture.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);
        var content = (Notes.NoteContent)intro.Content;
        content.Pending = true;
        bool? pendingWhenAsked = null;

        var proceed = await fixture.Service.SaveBeforeAsync(
            SaveBeforeOperation.Rename,
            _ =>
            {
                pendingWhenAsked = content.HasEditsInProgress;
                return Task.FromResult<IReadOnlyList<UnsavedDocument>>([]);
            },
            this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = proceed.Should().BeTrue();
        _ = pendingWhenAsked.Should().BeFalse();
        fixture.Committer.Verify(value => value.CommitAsync(), Times.Once);
    }

    private static UnsavedDocument Describe(Document document) => new(document.Id, document.File, document.Title);

    /// <summary>A window with a document area of notes and the document service over it.</summary>
    private sealed class Fixture : IDisposable
    {
        public Fixture(bool onePerArea = false, bool approveClose = true, FakeRelocations? relocations = null)
        {
            var descriptor = new DocumentEditorDescriptor(this.Notes.Kind, tab =>
            {
                var editor = new RecordingEditor(tab.Document);
                this.Editors.Add(editor);
                return editor;
            })
            { IsOnePerArea = onePerArea };
            this.Area = new DocumentArea(this.Tabs, this.Notes.Documents, [descriptor], Window, static _ => { });
            var prompt = new Mock<IDocumentClosePrompt>();
            _ = prompt.Setup(value => value.ConfirmAsync(It.IsAny<IReadOnlyList<DocumentCloseItem>>())).ReturnsAsync(approveClose);
            _ = this.Committer.Setup(value => value.CommitAsync()).Returns(Task.CompletedTask);
            var catalog = new Mock<IAssetCatalog>();
            _ = catalog.SetupGet(value => value.Changes).Returns(this.Changes);
            this.Service = new DocumentService(
                this.Area,
                this.Tabs,
                this.Notes.Documents,
                this.Notes.Locations,
                prompt.Object,
                this.Confirmations.Object,
                this.Committer.Object,
                this.Results.Object,
                catalog.Object,
                relocations);
        }

        public Notes Notes { get; } = new();

        public DocumentTabs Tabs { get; } = new();

        public DocumentArea Area { get; }

        public DocumentService Service { get; }

        public Subject<AssetChange> Changes { get; } = new();

        public Mock<IDocumentInputCommitter> Committer { get; } = new();

        public Mock<IDocumentConfirmations> Confirmations { get; } = new();

        public Mock<IOperationResultPublisher> Results { get; } = new();

        public List<RecordingEditor> Editors { get; } = [];

        public async Task<Document> OpenAsync(string name, CancellationToken cancellationToken)
        {
            await this.Notes.WriteAsync(name, name, cancellationToken).ConfigureAwait(false);
            var placement = await this.Service.OpenAsync(Notes.AssetOf(name), cancellationToken).ConfigureAwait(false);
            return placement.Document ?? throw new InvalidOperationException(placement.Problem);
        }

        public void Dispose()
        {
            this.Service.Dispose();
            this.Area.Dispose();
            this.Changes.Dispose();
            this.Notes.Dispose();
        }
    }

    /// <summary>Stands in for a note's editor: it saves its document and counts the saves.</summary>
    private sealed class RecordingEditor(Document document) : IAsyncSaveable, IDocumentCloseParticipant, IDisposable
    {
        public int Saves { get; private set; }

        public bool Disposed { get; private set; }

        public async Task SaveAsync()
        {
            this.Saves++;
            _ = await document.SaveAsync().ConfigureAwait(false);
        }

        public Task PrepareForCloseAsync() => Task.CompletedTask;

        public async Task<bool> SaveForCloseAsync() => (await document.SaveAsync().ConfigureAwait(false)).Succeeded;

        public Task CloseAsync(bool discard) => Task.CompletedTask;

        public void ResumeEditing()
        {
        }

        public void Dispose() => this.Disposed = true;
    }

    /// <summary>Relocations that a test makes happen.</summary>
    private sealed class FakeRelocations : IDocumentRelocations
    {
        private Func<Func<Document, DocumentRelocation?>, Task>? follower;

        public event EventHandler<DocumentFilesDeletedEventArgs>? FilesDeleted;

        public IDisposable Follow(Func<Func<Document, DocumentRelocation?>, Task> follow)
        {
            this.follower = follow;
            return new Unfollow(this);
        }

        public Task RelocateAsync(Func<Document, DocumentRelocation?> map) => this.follower?.Invoke(map) ?? Task.CompletedTask;

        public void Delete(FileSystemPath file) => this.FilesDeleted?.Invoke(this, new DocumentFilesDeletedEventArgs([file]));

        private sealed class Unfollow(FakeRelocations owner) : IDisposable
        {
            public void Dispose() => owner.follower = null;
        }
    }
}
