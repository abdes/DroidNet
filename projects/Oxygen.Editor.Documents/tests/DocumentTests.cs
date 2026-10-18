// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text;
using AwesomeAssertions;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Documents.Tests;

/// <summary>Tests that every document saves, reloads and follows its file the same way, whatever its kind.</summary>
[TestClass]
[TestCategory("Unit")]
[ExcludeFromCodeCoverage]
public sealed class DocumentTests : DocumentTestBase
{
    [TestMethod]
    public async Task IsDirty_AfterUndoBackToTheSavedState_IsFalse()
    {
        var (document, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        note.Edit("second");
        note.Edit("third");
        _ = (await document.SaveAsync(this.Token).ConfigureAwait(false)).Succeeded.Should().BeTrue();

        note.Edit("fourth");
        _ = document.IsDirty.Should().BeTrue();
        document.History.Undo();
        _ = document.IsDirty.Should().BeFalse();
        document.History.Undo();
        _ = document.IsDirty.Should().BeTrue();
        document.History.Redo();

        _ = document.IsDirty.Should().BeFalse();
        _ = note.Text.Should().Be("third");
    }

    [TestMethod]
    public async Task StateChanged_WhenAnEditMakesTheDocumentDirtyAndUndoCleansIt_IsRaisedEachTime()
    {
        var (document, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        var raised = 0;
        document.StateChanged += (_, _) => raised++;

        note.Edit("second");
        note.Edit("third");
        document.History.Undo();
        document.History.Undo();

        _ = raised.Should().Be(2);
    }

    [TestMethod]
    public async Task SaveAsync_WithEdits_WritesTheContentAndCleansTheDocument()
    {
        var (document, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        note.Edit("second");

        var result = await document.SaveAsync(this.Token).ConfigureAwait(false);

        _ = result.Outcome.Should().Be(DocumentSaveOutcome.Saved);
        _ = this.ReadText("Wood").Should().Be("second");
        _ = document.IsDirty.Should().BeFalse();
    }

    [TestMethod]
    public async Task SaveAsync_WithAnEditInProgress_SavesItAsItsOwnHistoryEntry()
    {
        var (document, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        note.Pending = "typed";

        _ = await document.SaveAsync(this.Token).ConfigureAwait(false);

        _ = this.ReadText("Wood").Should().Be("typed");
        _ = document.History.UndoStack.Should().ContainSingle();
        _ = document.IsDirty.Should().BeFalse();
    }

    [TestMethod]
    public async Task SaveAsync_WhenAnEditArrivesWhileTheFileIsWritten_KeepsTheDocumentDirty()
    {
        var (document, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        note.Edit("second");
        note.DuringEncode = () =>
        {
            note.DuringEncode = null;
            note.Edit("third");
        };

        _ = await document.SaveAsync(this.Token).ConfigureAwait(false);

        _ = this.ReadText("Wood").Should().Be("second");
        _ = document.IsDirty.Should().BeTrue();
        document.History.Undo();
        _ = document.IsDirty.Should().BeFalse();
    }

    [TestMethod]
    public async Task SaveAsync_WithoutEdits_WritesNothing()
    {
        var (document, _) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        var written = this.FileSystem.File.GetLastWriteTimeUtc(PathOf("Wood"));
        var version = document.Version;

        var result = await document.SaveAsync(this.Token).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = document.Version.Should().Be(version);
        _ = this.FileSystem.File.GetLastWriteTimeUtc(PathOf("Wood")).Should().Be(written);
    }

    [TestMethod]
    public async Task SaveAsync_WhenTheContentRefuses_WritesNothingAndStaysDirty()
    {
        var (document, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        note.Edit("second");
        note.Refusal = "The roughness is out of range.";

        var result = await document.SaveAsync(this.Token).ConfigureAwait(false);

        _ = result.Outcome.Should().Be(DocumentSaveOutcome.Refused);
        _ = result.Message.Should().Be("The roughness is out of range.");
        _ = this.ReadText("Wood").Should().Be("first");
        _ = document.IsDirty.Should().BeTrue();
    }

    [TestMethod]
    public async Task SaveAsync_AfterAnOutsideChange_ConflictsAndKeepsTheOutsideFile()
    {
        var (document, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        note.Edit("mine");
        this.WriteNote("Wood", "theirs");

        var result = await document.SaveAsync(this.Token).ConfigureAwait(false);

        _ = result.Outcome.Should().Be(DocumentSaveOutcome.Conflict);
        _ = this.ReadText("Wood").Should().Be("theirs");
        _ = document.IsChangedOnDisk.Should().BeTrue();
        _ = document.IsDirty.Should().BeTrue();
    }

    [TestMethod]
    public async Task KeepMineAsync_AfterAnOutsideChange_LetsTheNextSaveReplaceIt()
    {
        var (document, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        note.Edit("mine");
        this.WriteNote("Wood", "theirs");
        _ = await document.CheckFileAsync(this.Token).ConfigureAwait(false);

        await document.KeepMineAsync(this.Token).ConfigureAwait(false);
        _ = document.IsChangedOnDisk.Should().BeFalse();
        _ = document.IsDirty.Should().BeTrue();
        var result = await document.SaveAsync(this.Token).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = this.ReadText("Wood").Should().Be("mine");
    }

    [TestMethod]
    public async Task CheckFileAsync_AfterTheDocumentsOwnSave_FindsItUnchanged()
    {
        var (document, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        note.Edit("second");
        _ = await document.SaveAsync(this.Token).ConfigureAwait(false);
        var decodes = this.Kind.Decodes;

        var state = await document.CheckFileAsync(this.Token).ConfigureAwait(false);

        _ = state.Should().Be(DocumentFileState.Unchanged);
        _ = this.Kind.Decodes.Should().Be(decodes);
        _ = document.History.CanUndo.Should().BeTrue();
    }

    [TestMethod]
    public async Task CheckFileAsync_WhenACleanDocumentsFileChanged_ReloadsIt()
    {
        var (document, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        note.Edit("second");
        _ = await document.SaveAsync(this.Token).ConfigureAwait(false);
        DocumentContentReplacedEventArgs? replaced = null;
        document.ContentReplaced += (_, args) => replaced = args;
        this.WriteNote("Wood", "theirs");

        var state = await document.CheckFileAsync(this.Token).ConfigureAwait(false);

        _ = state.Should().Be(DocumentFileState.Reloaded);
        _ = ((NoteContent)document.Content).Text.Should().Be("theirs");
        _ = replaced.Should().NotBeNull();
        _ = replaced!.Previous.Should().BeSameAs(note);
        _ = note.IsDisposed.Should().BeTrue();
        _ = document.History.CanUndo.Should().BeFalse();
        _ = document.IsDirty.Should().BeFalse();
    }

    [TestMethod]
    public async Task CheckFileAsync_WhenADirtyDocumentsFileChanged_MarksItChangedOnDiskAndKeepsTheEdits()
    {
        var (document, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        note.Edit("mine");
        this.WriteNote("Wood", "theirs");

        var state = await document.CheckFileAsync(this.Token).ConfigureAwait(false);

        _ = state.Should().Be(DocumentFileState.ChangedOnDisk);
        _ = document.Content.Should().BeSameAs(note);
        _ = note.Text.Should().Be("mine");
        _ = document.IsChangedOnDisk.Should().BeTrue();
    }

    [TestMethod]
    public async Task CheckFileAsync_WhenTheFileChangesDuringAnEditInProgress_KeepsTheEdit()
    {
        var (document, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        note.Pending = "typing";
        this.WriteNote("Wood", "theirs");

        var state = await document.CheckFileAsync(this.Token).ConfigureAwait(false);

        _ = state.Should().Be(DocumentFileState.ChangedOnDisk);
        _ = document.Content.Should().BeSameAs(note);
        _ = note.Pending.Should().Be("typing");
    }

    [TestMethod]
    public async Task CheckFileAsync_WhenTheChangedFileDoesNotDecode_MarksItChangedOnDisk()
    {
        var (document, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        this.WriteNote("Wood", NoteKind.Unreadable);

        var state = await document.CheckFileAsync(this.Token).ConfigureAwait(false);

        _ = state.Should().Be(DocumentFileState.ChangedOnDisk);
        _ = document.Content.Should().BeSameAs(note);
        _ = note.IsDisposed.Should().BeFalse();
    }

    [TestMethod]
    public async Task CheckFileAsync_WhenTheFileIsDeleted_KeepsTheDocumentOpenAndMissing()
    {
        var (document, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        this.FileSystem.File.Delete(PathOf("Wood"));

        var state = await document.CheckFileAsync(this.Token).ConfigureAwait(false);

        _ = state.Should().Be(DocumentFileState.Missing);
        _ = document.IsMissingOnDisk.Should().BeTrue();
        _ = document.IsDirty.Should().BeTrue();
        _ = document.Content.Should().BeSameAs(note);
    }

    [TestMethod]
    public async Task SaveAsync_WhenTheFileIsMissing_WritesItAgain()
    {
        var (document, _) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        this.FileSystem.File.Delete(PathOf("Wood"));
        _ = await document.CheckFileAsync(this.Token).ConfigureAwait(false);

        var result = await document.SaveAsync(this.Token).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = this.ReadText("Wood").Should().Be("first");
        _ = document.IsDirty.Should().BeFalse();
    }

    [TestMethod]
    public async Task CheckFileAsync_WhenAMissingFileComesBackAsItWas_RestoresTheDocument()
    {
        var (document, _) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        var bytes = this.FileSystem.File.ReadAllBytes(PathOf("Wood"));
        this.FileSystem.File.Delete(PathOf("Wood"));
        _ = await document.CheckFileAsync(this.Token).ConfigureAwait(false);
        this.FileSystem.File.WriteAllBytes(PathOf("Wood"), bytes);

        var state = await document.CheckFileAsync(this.Token).ConfigureAwait(false);

        _ = state.Should().Be(DocumentFileState.Restored);
        _ = document.IsMissingOnDisk.Should().BeFalse();
        _ = document.IsDirty.Should().BeFalse();
    }

    [TestMethod]
    public async Task ReloadAsync_WithEdits_DiscardsThemAndTheHistory()
    {
        var (document, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        note.Edit("mine");

        var result = await document.ReloadAsync(this.Token).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = ((NoteContent)document.Content).Text.Should().Be("first");
        _ = document.History.CanUndo.Should().BeFalse();
        _ = document.IsDirty.Should().BeFalse();
    }

    [TestMethod]
    public async Task ReloadAsync_WhenTheFileDoesNotDecode_KeepsTheContent()
    {
        var (document, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        note.Edit("mine");
        this.WriteNote("Wood", NoteKind.Unreadable);

        var result = await document.ReloadAsync(this.Token).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.Problem.Should().Contain("is not a note");
        _ = document.Content.Should().BeSameAs(note);
        _ = document.History.CanUndo.Should().BeTrue();
    }

    [TestMethod]
    public async Task FollowRelocationAsync_KeepsTheHistoryAndTheSavedState()
    {
        var oak = UriOf("Oak");
        var (document, note) = await this.OpenNoteAsync("Wood", "first", oak).ConfigureAwait(false);
        note.Edit("second");
        _ = await document.SaveAsync(this.Token).ConfigureAwait(false);
        var pine = UriOf("Pine");
        this.FileSystem.File.Move(PathOf("Wood"), PathOf("Timber"));
        this.FileSystem.File.WriteAllBytes(PathOf("Timber"), NoteKind.Encode(UriOf("Timber"), "second", pine));
        var written = (await this.Store.ReadAsync(PathOf("Timber"), this.Token).ConfigureAwait(false)).Version;

        await document.FollowRelocationAsync(
            new DocumentRelocation(FileOf("Timber"), written, Moves(new Dictionary<AssetPath, AssetPath> { [UriOf("Wood")] = UriOf("Timber"), [oak] = pine })),
            this.Token).ConfigureAwait(false);

        _ = document.Title.Should().Be("Timber");
        _ = note.Identity.Should().Be(UriOf("Timber"));
        _ = note.References.Should().Equal(pine);
        _ = document.IsDirty.Should().BeFalse();
        _ = document.History.UndoStack.Should().ContainSingle();
        _ = this.Documents.Find(PathOf("Timber")).Should().BeSameAs(document);
        _ = this.Documents.Find(PathOf("Wood")).Should().BeNull();
        note.Edit("third");
        _ = (await document.SaveAsync(this.Token).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = this.ReadText("Timber").Should().Be("third");
    }

    [TestMethod]
    public async Task FollowRelocationAsync_WithEdits_Throws()
    {
        var (document, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        note.Edit("mine");

        var follow = () => document.FollowRelocationAsync(
            new DocumentRelocation(FileOf("Timber"), document.Version, static _ => null),
            this.Token);

        _ = await follow.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
        _ = document.File.Should().Be(FileOf("Wood"));
    }

    [TestMethod]
    public async Task SaveCopyAsync_WritesTheContentUnderTheCopysIdentityAndLeavesTheDocumentAsItWas()
    {
        var (document, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        note.Edit("mine");

        var result = await document.SaveCopyAsync(FileOf("Wood Copy"), this.Token).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        var copy = this.FileSystem.File.ReadAllText(PathOf("Wood Copy"), Encoding.UTF8).Split('\n');
        _ = copy[0].Should().Be(UriOf("Wood Copy").ToUriString());
        _ = copy[1].Should().Be("mine");
        _ = this.ReadText("Wood").Should().Be("first");
        _ = document.IsDirty.Should().BeTrue();
        _ = document.File.Should().Be(FileOf("Wood"));
    }

    [TestMethod]
    public async Task SaveCopyAsync_OverAnExistingFile_ConflictsAndWritesNothing()
    {
        var (document, _) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        this.WriteNote("Oak", "oak");

        var result = await document.SaveCopyAsync(FileOf("Oak"), this.Token).ConfigureAwait(false);

        _ = result.Outcome.Should().Be(DocumentSaveOutcome.Conflict);
        _ = this.ReadText("Oak").Should().Be("oak");
    }

    private static Func<string, string?> Moves(Dictionary<AssetPath, AssetPath> moves)
        => reference => moves.TryGetValue(AssetPath.ParseUri(reference), out var moved) ? moved.ToUriString() : null;
}
