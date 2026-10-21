// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.TimeMachine;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Shell.Unit.Tests.Documents;

/// <summary>The open documents with unsaved changes follow the documents' edits, saves and closes.</summary>
[TestClass]
public sealed class UnsavedDocumentsTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A clean document is not reported.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ACleanDocumentIsNotReported()
    {
        using var notes = new Notes();
        using var unsaved = new UnsavedDocuments(notes.Documents);
        _ = await notes.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = unsaved.Find([Notes.PathOf("Intro")]).Should().BeEmpty();
    }

    /// <summary>An edited document is reported with its identity, file and title, and the change is raised.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AnEditedDocumentIsReportedAndRaisesChanged()
    {
        using var notes = new Notes();
        using var unsaved = new UnsavedDocuments(notes.Documents);
        var intro = await notes.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);
        var raised = 0;
        unsaved.Changed += (_, _) => raised++;

        intro.History.AddChange("Edit", static () => { });

        var found = unsaved.Find([Notes.PathOf("Intro")]);
        _ = found.Should().ContainSingle();
        _ = found[0].Id.Should().Be(intro.Id);
        _ = found[0].File.Should().Be(intro.File);
        _ = found[0].Title.Should().Be(intro.Title);
        _ = raised.Should().BeGreaterThan(0);
    }

    /// <summary>A folder names the documents inside it, and another path names none of them.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AFolderFindsTheDocumentsInsideItAndAnotherPathDoesNot()
    {
        using var notes = new Notes();
        using var unsaved = new UnsavedDocuments(notes.Documents);
        var intro = await notes.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);
        intro.History.AddChange("Edit", static () => { });
        var folder = FileSystemPath.From(Path.GetDirectoryName(Notes.PathOf("Intro").FullPath)!);
        var elsewhere = FileSystemPath.From(Path.Combine(Path.GetTempPath(), "elsewhere"));

        _ = unsaved.Find([folder]).Should().ContainSingle();
        _ = unsaved.Find([elsewhere]).Should().BeEmpty();
        _ = unsaved.Find([Notes.PathOf("Outro")]).Should().BeEmpty();
    }

    /// <summary>Saving the document ends its report and raises the change.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ASavedDocumentIsNoLongerReported()
    {
        using var notes = new Notes();
        using var unsaved = new UnsavedDocuments(notes.Documents);
        var intro = await notes.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);
        intro.History.AddChange("Edit", static () => { });
        var raised = 0;
        unsaved.Changed += (_, _) => raised++;

        var saved = await intro.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = saved.Succeeded.Should().BeTrue(saved.Message);
        _ = unsaved.Find([Notes.PathOf("Intro")]).Should().BeEmpty();
        _ = raised.Should().BeGreaterThan(0);
    }

    /// <summary>A document that closes with its changes is gone from the report.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AClosedDocumentIsGone()
    {
        using var notes = new Notes();
        using var unsaved = new UnsavedDocuments(notes.Documents);
        var intro = await notes.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);
        intro.History.AddChange("Edit", static () => { });
        var raised = 0;
        unsaved.Changed += (_, _) => raised++;

        notes.Documents.Close(intro);

        _ = unsaved.Find([Notes.PathOf("Intro")]).Should().BeEmpty();
        _ = raised.Should().BeGreaterThan(0);
    }
}
