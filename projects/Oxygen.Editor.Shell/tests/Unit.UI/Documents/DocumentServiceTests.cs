// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Concurrency;
using System.Reactive.Subjects;
using AwesomeAssertions;
using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using DroidNet.Tests;
using DroidNet.TimeMachine;
using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Moq;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Unit.Tests.Documents;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Shell.Unit.UI.Tests.Documents;

/// <summary>
/// The session's document service and a window's document area over it: the area opens a file once and places its
/// document, the service owns the editors, saves through them, and keeps documents in step with their files.
/// </summary>
[TestClass]
[TestCategory("UITest")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository's discovery configuration.")]
public sealed partial class DocumentServiceTests : VisualUserInterfaceTests
{
    private static readonly WindowId Window = new(5);

    /// <summary>Opening an asset twice shows its one document, whose editor the kind's descriptor made once.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task AnOpenAssetIsShownWithTheEditorItsKindMade() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        await fixture.Notes.WriteAsync("Intro", "Intro", CancellationToken.None).ConfigureAwait(true);

        var first = await fixture.Area.OpenAsync(Notes.AssetOf("Intro")).ConfigureAwait(true);
        var second = await fixture.Area.OpenAsync(Notes.AssetOf("Intro")).ConfigureAwait(true);

        _ = first.Succeeded.Should().BeTrue(first.Problem);
        _ = second.Document.Should().BeSameAs(first.Document);
        _ = fixture.Notes.Documents.Documents.Should().ContainSingle();
        _ = fixture.Editors.Should().ContainSingle();
        _ = fixture.Area.ActiveEditor.Should().BeSameAs(fixture.Editors[0]);
        _ = fixture.Service.EditorOf(first.Document!).Should().BeSameAs(fixture.Editors[0]);
    });

    /// <summary>A kind with one document per area closes the shown document before it shows the next one.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task AKindWithOneDocumentPerAreaReplacesTheShownOne() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture(onePerArea: true);
        var intro = await fixture.OpenAsync("Intro").ConfigureAwait(true);
        await fixture.Notes.WriteAsync("Outro", "Outro", CancellationToken.None).ConfigureAwait(true);

        var placement = await fixture.Area.OpenAsync(Notes.AssetOf("Outro")).ConfigureAwait(true);

        _ = placement.Succeeded.Should().BeTrue(placement.Problem);
        _ = placement.Replaced.Should().Equal(intro.File.Asset);
        _ = fixture.Area.Documents.Should().Equal(placement.Document!);
        _ = fixture.Notes.Documents.Documents.Should().Equal(placement.Document!);
        _ = fixture.Editors[0].Disposed.Should().BeTrue("the replaced document closed");
        _ = fixture.Area.ActiveEditor.Should().BeSameAs(fixture.Editors[1]);
    });

    /// <summary>Cancelling the question about the shown document's changes opens nothing and keeps it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task ACancelledReplacementKeepsTheShownDocument() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture(onePerArea: true, approveClose: false);
        var intro = await fixture.OpenAsync("Intro").ConfigureAwait(true);
        intro.History.AddChange("Edit", static () => { });
        await fixture.Notes.WriteAsync("Outro", "Outro", CancellationToken.None).ConfigureAwait(true);

        var placement = await fixture.Area.OpenAsync(Notes.AssetOf("Outro")).ConfigureAwait(true);

        _ = placement.Succeeded.Should().BeFalse();
        _ = placement.Problem.Should().BeNull("the user cancelled");
        _ = fixture.Area.Documents.Should().Equal(intro);
        _ = intro.IsDirty.Should().BeTrue();
        fixture.Results.Verify(value => value.Publish(It.IsAny<OperationResult>()), Times.Never);
    });

    /// <summary>A file that does not open keeps the shown document and publishes why.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task AFileThatDoesNotOpenKeepsTheShownDocument() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture(onePerArea: true);
        var intro = await fixture.OpenAsync("Intro").ConfigureAwait(true);

        var placement = await fixture.Area.OpenAsync(Notes.AssetOf("Missing")).ConfigureAwait(true);

        _ = placement.Succeeded.Should().BeFalse();
        _ = placement.Problem.Should().NotBeNull();
        _ = placement.Replaced.Should().BeEmpty();
        _ = fixture.Area.Documents.Should().Equal(intro);
        fixture.Results.Verify(
            value => value.Publish(It.Is<OperationResult>(result => result.Diagnostics.Any(diagnostic => diagnostic.Code == DiagnosticCodes.DocumentPrefix + "OPEN_FAILED"))),
            Times.Once);
    });

    /// <summary>Saving by asset saves the open documents of those assets through their editors, and skips the others.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task SavingByAssetSavesThroughTheEditors() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        var intro = await fixture.OpenAsync("Intro").ConfigureAwait(true);
        intro.History.AddChange("Edit", static () => { });

        var saved = await fixture.Service.SaveAsync([intro.File.Asset, Notes.AssetOf("Closed")]).ConfigureAwait(true);

        _ = saved.Should().BeTrue();
        _ = intro.IsDirty.Should().BeFalse();
        _ = fixture.Editors.Should().ContainSingle().Which.Saves.Should().Be(1);
    });

    /// <summary>Saving all of a window's documents saves the unsaved ones it shows, through their editors.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task SavingAllOfAWindowSavesItsUnsavedDocuments() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        var intro = await fixture.OpenAsync("Intro").ConfigureAwait(true);
        var outro = await fixture.OpenAsync("Outro").ConfigureAwait(true);
        intro.History.AddChange("Edit", static () => { });

        var saved = await fixture.Area.SaveAllAsync().ConfigureAwait(true);

        _ = saved.Should().BeTrue();
        _ = intro.IsDirty.Should().BeFalse();
        _ = outro.IsDirty.Should().BeFalse();
        _ = fixture.Editors.Sum(static editor => editor.Saves).Should().Be(1, "only the unsaved document saves");
    });

    /// <summary>A relocation moves the documents it maps, and a delete in the editor closes the deleted file's document.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task DocumentsFollowRelocationsAndCloseWhenDeleted() => EnqueueAsync(async () =>
    {
        var relocations = new FakeRelocations();
        using var fixture = new Fixture(relocations: relocations);
        var intro = await fixture.OpenAsync("Intro").ConfigureAwait(true);
        var outro = await fixture.OpenAsync("Outro").ConfigureAwait(true);
        var moved = new DocumentFile(Notes.PathOf("Moved"), Notes.AssetOf("Moved"));

        await relocations.RelocateAsync(document => ReferenceEquals(document, intro)
            ? new DocumentRelocation(moved, document.Version, static _ => null)
            : null).ConfigureAwait(true);
        relocations.Delete(outro.File.Path);
        await WaitUntilAsync(() => fixture.Area.Documents.Count == 1).ConfigureAwait(true);

        _ = intro.File.Should().Be(moved);
        _ = fixture.Area.Documents.Should().Equal(intro);
        _ = fixture.Notes.Documents.Documents.Should().Equal(intro);
    });

    /// <summary>
    /// A change on disk reloads a clean document, and marks an edited one changed on disk, which Keep Mine resolves. The
    /// in-memory files answer at once, so each check completes before the change is handed on.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task OutsideChangesReloadCleanDocumentsAndMarkEditedOnes() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        var clean = await fixture.OpenAsync("Clean").ConfigureAwait(true);
        var edited = await fixture.OpenAsync("Edited").ConfigureAwait(true);
        edited.History.AddChange("Edit", static () => { });
        var reloads = 0;
        clean.ContentReplaced += (_, _) => reloads++;

        await fixture.Notes.WriteAsync("Clean", "changed", CancellationToken.None).ConfigureAwait(true);
        await fixture.Notes.WriteAsync("Edited", "changed", CancellationToken.None).ConfigureAwait(true);
        fixture.Changes.OnNext(new AssetChange(AssetChangeKind.Updated, clean.File.Asset));
        fixture.Changes.OnNext(new AssetChange(AssetChangeKind.Updated, edited.File.Asset));

        _ = reloads.Should().Be(1);
        _ = edited.IsChangedOnDisk.Should().BeTrue();
        _ = edited.IsDirty.Should().BeTrue();

        await fixture.Service.KeepMineAsync(edited).ConfigureAwait(true);
        _ = edited.IsChangedOnDisk.Should().BeFalse();
        _ = edited.IsDirty.Should().BeTrue();
    });

    /// <summary>Saving a copy writes a new file beside the document and leaves the document unsaved.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task ACopyIsSavedBesideTheDocumentWhichStaysUnsaved() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        var intro = await fixture.OpenAsync("Intro").ConfigureAwait(true);
        intro.History.AddChange("Edit", static () => { });

        var result = await fixture.Service.SaveCopyAsync(intro, "IntroCopy").ConfigureAwait(true);

        _ = result.Succeeded.Should().BeTrue(result.Message);
        _ = (await fixture.Notes.Files.ReadAsync(Notes.PathOf("IntroCopy").FullPath, CancellationToken.None).ConfigureAwait(true)).Version.Exists.Should().BeTrue();
        _ = intro.IsDirty.Should().BeTrue();
        fixture.Committer.Verify(value => value.CommitAsync(intro.Id), Times.Once);
    });

    /// <summary>With nothing unsaved, an operation that reads saved files goes on without asking.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task SaveBeforeWithNothingUnsavedContinuesWithoutAsking() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        _ = await fixture.OpenAsync("Intro").ConfigureAwait(true);

        var proceed = await fixture.Area.SaveBeforeAsync(SaveBeforeOperation.Play, static _ => Task.FromResult<IReadOnlyList<UnsavedDocument>>([])).ConfigureAwait(true);

        _ = proceed.Should().BeTrue();
        fixture.Confirmations.Verify(value => value.ConfirmSaveBeforeAsync(It.IsAny<WindowId>(), It.IsAny<IReadOnlyList<UnsavedDocument>>(), It.IsAny<SaveBeforeOperation>()), Times.Never);
    });

    /// <summary>Cancelling the question stops the operation and leaves the documents unsaved.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task SaveBeforeCancelledStopsTheOperationAndKeepsTheChanges() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        var intro = await fixture.OpenAsync("Intro").ConfigureAwait(true);
        intro.History.AddChange("Edit", static () => { });
        _ = fixture.Confirmations.Setup(value => value.ConfirmSaveBeforeAsync(Window, It.IsAny<IReadOnlyList<UnsavedDocument>>(), SaveBeforeOperation.Cook)).ReturnsAsync(value: false);

        var proceed = await fixture.Area.SaveBeforeAsync(SaveBeforeOperation.Cook, _ => Task.FromResult<IReadOnlyList<UnsavedDocument>>([Describe(intro)])).ConfigureAwait(true);

        _ = proceed.Should().BeFalse();
        _ = intro.IsDirty.Should().BeTrue();
        _ = fixture.Editors.Should().ContainSingle().Which.Saves.Should().Be(0);
    });

    /// <summary>Confirming the question in the asking window saves the documents it listed and lets the operation go on.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task SaveBeforeConfirmedSavesTheListedDocuments() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        var intro = await fixture.OpenAsync("Intro").ConfigureAwait(true);
        var outro = await fixture.OpenAsync("Outro").ConfigureAwait(true);
        intro.History.AddChange("Edit", static () => { });
        outro.History.AddChange("Edit", static () => { });
        _ = fixture.Confirmations.Setup(value => value.ConfirmSaveBeforeAsync(Window, It.IsAny<IReadOnlyList<UnsavedDocument>>(), SaveBeforeOperation.Play)).ReturnsAsync(value: true);

        var proceed = await fixture.Area.SaveBeforeAsync(SaveBeforeOperation.Play, _ => Task.FromResult<IReadOnlyList<UnsavedDocument>>([Describe(intro)])).ConfigureAwait(true);

        _ = proceed.Should().BeTrue();
        _ = intro.IsDirty.Should().BeFalse();
        _ = outro.IsDirty.Should().BeTrue("the operation did not need it");
    });

    /// <summary>The operation looks for unsaved documents after the asking window's edits in progress were finished.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task SaveBeforeFinishesEditsInProgressBeforeLookingForUnsavedDocuments() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        var intro = await fixture.OpenAsync("Intro").ConfigureAwait(true);
        var content = (Notes.NoteContent)intro.Content;
        content.Pending = true;
        bool? pendingWhenAsked = null;

        var proceed = await fixture.Area.SaveBeforeAsync(
            SaveBeforeOperation.Rename,
            _ =>
            {
                pendingWhenAsked = content.HasEditsInProgress;
                return Task.FromResult<IReadOnlyList<UnsavedDocument>>([]);
            }).ConfigureAwait(true);

        _ = proceed.Should().BeTrue();
        _ = pendingWhenAsked.Should().BeFalse();
        fixture.Committer.Verify(value => value.CommitAsync(Window), Times.Once);
    });

    /// <summary>
    /// The editor of a document gets the services of the window that shows its tab, and loses them before it is disposed
    /// when the tab closes.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task AnEditorHasItsWindowsServicesWhileItsTabIsShown() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        using var attachments = new DocumentWindowAttachments(fixture.Area, fixture.Service, Mock.Of<ICommandService>(), Mock.Of<IPlacementService>());
        var intro = await fixture.OpenAsync("Intro").ConfigureAwait(true);
        var editor = fixture.Editors.Single();

        _ = editor.Window.Should().NotBeNull();
        _ = editor.Window!.Window.Should().Be(Window);
        _ = editor.Window.Area.Should().BeSameAs(fixture.Area);

        _ = (await fixture.Area.CloseAsync(intro).ConfigureAwait(true)).Should().BeTrue();

        _ = editor.Window.Should().BeNull();
        _ = editor.DetachedBeforeDisposed.Should().BeTrue();
        _ = editor.Disposed.Should().BeTrue();
    });

    private static UnsavedDocument Describe(Document document) => new(document.Id, document.File, document.Title);

    private static async Task WaitUntilAsync(Func<bool> condition)
    {
        for (var attempt = 0; !condition() && attempt < 100; attempt++)
        {
            await Task.Delay(10).ConfigureAwait(true);
        }
    }

    /// <summary>A session's document service, and a window with a document area of notes over it.</summary>
    private sealed partial class Fixture : IDisposable
    {
        private readonly IDisposable[] owned;

        public Fixture(bool onePerArea = false, bool approveClose = true, FakeRelocations? relocations = null)
        {
            var descriptor = new DocumentEditorDescriptor(this.Notes.Kind, tab =>
            {
                var editor = new RecordingEditor(tab.Document);
                this.Editors.Add(editor);
                return editor;
            })
            { IsOnePerArea = onePerArea };
            var prompt = new Mock<IDocumentClosePrompt>();
            _ = prompt.Setup(value => value.ConfirmAsync(It.IsAny<WindowId>(), It.IsAny<IReadOnlyList<DocumentCloseItem>>())).ReturnsAsync(approveClose);
            _ = this.Committer.Setup(value => value.CommitAsync(It.IsAny<WindowId>())).Returns(Task.CompletedTask);
            _ = this.Committer.Setup(value => value.CommitAsync(It.IsAny<DocumentId>())).Returns(Task.CompletedTask);
            var catalog = new Mock<IAssetCatalog>();
            _ = catalog.SetupGet(value => value.Changes).Returns(this.Changes);
            var hosting = new HostingContext
            {
                Dispatcher = DispatcherQueue.GetForCurrentThread(),
                Application = Application.Current,
                DispatcherScheduler = new DispatcherQueueScheduler(DispatcherQueue.GetForCurrentThread()),
            };
            this.Service = new DocumentService(
                this.Tabs,
                this.Notes.Documents,
                this.Notes.Locations,
                prompt.Object,
                this.Confirmations.Object,
                this.Committer.Object,
                this.Results.Object,
                hosting,
                catalog.Object,
                relocations);
            this.Area = new DocumentArea(
                this.Tabs,
                this.Notes.Documents,
                new DocumentEditors(this.Notes.Documents, [descriptor]),
                this.Service,
                this.Notes.Locations,
                this.Results.Object,
                Window,
                static _ => { });
            this.owned = [this.Area, this.Service, this.Changes, this.Notes];
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

        public async Task<Document> OpenAsync(string name)
        {
            await this.Notes.WriteAsync(name, name, CancellationToken.None).ConfigureAwait(true);
            var placement = await this.Area.OpenAsync(Notes.AssetOf(name)).ConfigureAwait(true);
            return placement.Document ?? throw new InvalidOperationException(placement.Problem);
        }

        public void Dispose()
        {
            foreach (var disposable in this.owned)
            {
                disposable.Dispose();
            }
        }
    }

    /// <summary>Stands in for a note's editor: it saves its document, counts the saves, and records its window.</summary>
    private sealed partial class RecordingEditor(Document document) : IAsyncSaveable, IDocumentCloseParticipant, IDocumentWindowEditor, IDisposable
    {
        public int Saves { get; private set; }

        public bool Disposed { get; private set; }

        public DocumentWindowServices? Window { get; private set; }

        public bool DetachedBeforeDisposed { get; private set; }

        public async Task SaveAsync()
        {
            this.Saves++;
            _ = await document.SaveAsync().ConfigureAwait(true);
        }

        public Task PrepareForCloseAsync() => Task.CompletedTask;

        public async Task<bool> SaveForCloseAsync() => (await document.SaveAsync().ConfigureAwait(true)).Succeeded;

        public Task CloseAsync(bool discard) => Task.CompletedTask;

        public void ResumeEditing()
        {
        }

        public void AttachWindow(DocumentWindowServices window) => this.Window = window;

        public void DetachWindow()
        {
            this.Window = null;
            this.DetachedBeforeDisposed = !this.Disposed;
        }

        public void Dispose() => this.Disposed = true;
    }

    /// <summary>Relocations that a test makes happen.</summary>
    private sealed partial class FakeRelocations : IDocumentRelocations
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

        private sealed partial class Unfollow(FakeRelocations owner) : IDisposable
        {
            public void Dispose() => owner.follower = null;
        }
    }
}
