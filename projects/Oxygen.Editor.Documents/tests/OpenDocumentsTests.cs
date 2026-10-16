// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace Oxygen.Editor.Documents.Tests;

/// <summary>Tests that a file opens once, with the kind its suffix names.</summary>
[TestClass]
[TestCategory("Unit")]
[ExcludeFromCodeCoverage]
public sealed class OpenDocumentsTests : DocumentTestBase
{
    [TestMethod]
    public async Task OpenAsync_ForAnOpenFile_FindsItsDocument()
    {
        var (document, _) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        var sameFile = new DocumentFile(PathOf("Wood").ToUpperInvariant(), UriOf("Wood"));

        var result = await this.Documents.OpenAsync(sameFile, this.Token).ConfigureAwait(false);

        _ = result.WasOpen.Should().BeTrue();
        _ = result.Document.Should().BeSameAs(document);
        _ = this.Documents.Documents.Should().ContainSingle();
    }

    [TestMethod]
    public async Task OpenAsync_RequestedTwiceAtOnce_OpensOneDocument()
    {
        this.WriteNote("Wood", "first");

        var results = await Task.WhenAll(
            this.Documents.OpenAsync(FileOf("Wood"), this.Token),
            this.Documents.OpenAsync(FileOf("Wood"), this.Token)).ConfigureAwait(false);

        _ = results[0].Document.Should().NotBeNull().And.BeSameAs(results[1].Document);
        _ = this.Documents.Documents.Should().ContainSingle();
        _ = this.Kind.Decodes.Should().Be(1);
    }

    [TestMethod]
    public async Task OpenAsync_ForAFileNoKindOpens_Fails()
    {
        this.FileSystem.File.WriteAllText(Path.Combine(Folder, "readme.md"), "text");

        var result = await this.Documents.OpenAsync(new DocumentFile(Path.Combine(Folder, "readme.md"), new Uri("asset:///Content/readme.md")), this.Token).ConfigureAwait(false);

        _ = result.Document.Should().BeNull();
        _ = result.Problem.Should().Contain("No editor opens 'readme.md'");
    }

    [TestMethod]
    public async Task OpenAsync_ForAMissingFile_Fails()
    {
        var result = await this.Documents.OpenAsync(FileOf("Wood"), this.Token).ConfigureAwait(false);

        _ = result.Document.Should().BeNull();
        _ = result.Problem.Should().Contain("does not exist");
    }

    [TestMethod]
    public async Task OpenAsync_ForAFileThatDoesNotDecode_FailsWithTheKindsProblem()
    {
        this.WriteNote("Wood", NoteKind.Unreadable);

        var result = await this.Documents.OpenAsync(FileOf("Wood"), this.Token).ConfigureAwait(false);

        _ = result.Document.Should().BeNull();
        _ = result.Problem.Should().Contain("is not a note");
        _ = this.Documents.Documents.Should().BeEmpty();
    }

    [TestMethod]
    public async Task OpenAsync_ANewDocument_StartsCleanWithAnEmptyHistory()
    {
        var (document, _) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);

        _ = document.IsDirty.Should().BeFalse();
        _ = document.History.CanUndo.Should().BeFalse();
        _ = document.Title.Should().Be("Wood");
        _ = document.Kind.Should().BeSameAs(this.Kind);
    }

    [TestMethod]
    public void FindKind_WhenSuffixesOverlap_ChoosesTheLongest()
    {
        var json = new NoteKind(".json") { Name = "Json" };
        var material = new NoteKind(".omat.json") { Name = "Material" };
        using var documents = new OpenDocuments([json, material], this.Store);

        _ = documents.FindKind(@"C:\Project\Content\Wood.OMAT.json").Should().BeSameAs(material);
        _ = documents.FindKind(@"C:\Project\Content\settings.json").Should().BeSameAs(json);
        _ = documents.FindKind(@"C:\Project\Content\.json").Should().BeNull();
    }

    [TestMethod]
    public void Constructor_WhenTwoKindsOpenOneSuffix_Throws()
    {
        var create = () => new OpenDocuments([new NoteKind(".note.txt"), new NoteKind(".NOTE.txt") { Name = "Other" }], this.Store);

        _ = create.Should().Throw<ArgumentException>().WithMessage("*both open*");
    }

    [TestMethod]
    public void Constructor_WhenASuffixHasNoDot_Throws()
    {
        var create = () => new OpenDocuments([new NoteKind("note")], this.Store);

        _ = create.Should().Throw<ArgumentException>().WithMessage("*does not start with a dot*");
    }

    [TestMethod]
    public async Task Close_DisposesTheDocumentAndItsContent()
    {
        var (document, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        Document? closed = null;
        this.Documents.Closed += (_, args) => closed = args.Document;

        this.Documents.Close(document);

        _ = closed.Should().BeSameAs(document);
        _ = note.IsDisposed.Should().BeTrue();
        _ = this.Documents.Find(PathOf("Wood")).Should().BeNull();
    }
}
