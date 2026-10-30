// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Storage.Native;

namespace Oxygen.Editor.Documents.Tests;

/// <summary>
/// The same file open in two editor sessions, as when two projects mount one shared folder: there is no lock, a session
/// saves only over the version it read, and the other session's save is an outside change to it. A clean document
/// reloads; one with unsaved edits is marked changed on disk and its save is refused until the user chooses.
/// </summary>
[TestClass]
[TestCategory("Unit")]
[ExcludeFromCodeCoverage]
public sealed class SharedFileSessionsTests : DocumentTestBase
{
    private readonly OpenDocuments otherSession;

    public SharedFileSessionsTests()
    {
        this.otherSession = new OpenDocuments([new NoteKind()], new NativeAtomicFileStore(this.FileSystem));
    }

    [TestMethod]
    public async Task ASaveInOneSession_ReloadsTheCleanDocumentOfTheOther()
    {
        var (mine, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        var theirs = await this.OpenInOtherSessionAsync("Wood").ConfigureAwait(false);
        note.Edit("saved in A");

        _ = (await mine.SaveAsync(this.Token).ConfigureAwait(false)).Outcome.Should().Be(DocumentSaveOutcome.Saved);
        var state = await theirs.CheckFileAsync(this.Token).ConfigureAwait(false);

        _ = state.Should().Be(DocumentFileState.Reloaded);
        _ = ((NoteContent)theirs.Content).Text.Should().Be("saved in A");
        _ = theirs.IsDirty.Should().BeFalse();
    }

    [TestMethod]
    public async Task ASaveInOneSession_RefusesTheOthersSaveOfItsUnsavedEditsUntilItKeepsThem()
    {
        var (mine, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        var theirs = await this.OpenInOtherSessionAsync("Wood").ConfigureAwait(false);
        var theirNote = (NoteContent)theirs.Content;
        theirNote.History = theirs.History;
        theirNote.Edit("unsaved in B");
        note.Edit("saved in A");
        _ = await mine.SaveAsync(this.Token).ConfigureAwait(false);

        var state = await theirs.CheckFileAsync(this.Token).ConfigureAwait(false);
        var refused = await theirs.SaveAsync(this.Token).ConfigureAwait(false);

        _ = state.Should().Be(DocumentFileState.ChangedOnDisk);
        _ = theirs.IsChangedOnDisk.Should().BeTrue();
        _ = theirNote.Text.Should().Be("unsaved in B", "the edits are kept");
        _ = refused.Outcome.Should().Be(DocumentSaveOutcome.Conflict);
        _ = this.ReadText("Wood").Should().Be("saved in A", "a session saves only over the version it read");

        await theirs.KeepMineAsync(this.Token).ConfigureAwait(false);
        _ = (await theirs.SaveAsync(this.Token).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = this.ReadText("Wood").Should().Be("unsaved in B");
    }

    [TestMethod]
    public async Task AFileTheOtherSessionMoved_LeavesTheDocumentOpenAndMissingOnDisk()
    {
        _ = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        var theirs = await this.OpenInOtherSessionAsync("Wood").ConfigureAwait(false);
        this.FileSystem.File.Move(PathOf("Wood"), PathOf("Oak"));

        var state = await theirs.CheckFileAsync(this.Token).ConfigureAwait(false);

        _ = state.Should().Be(DocumentFileState.Missing);
        _ = theirs.IsMissingOnDisk.Should().BeTrue();
        _ = theirs.IsDirty.Should().BeTrue("saving it again writes the file back");
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            this.otherSession.Dispose();
        }

        base.Dispose(disposing);
    }

    private async Task<Document> OpenInOtherSessionAsync(string name)
    {
        var result = await this.otherSession.OpenAsync(FileOf(name), this.Token).ConfigureAwait(false);
        return result.Document ?? throw new InvalidOperationException(result.Problem);
    }
}
