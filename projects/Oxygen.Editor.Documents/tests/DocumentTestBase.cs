// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Documents.Tests;

/// <summary>Opens note documents over the real atomic file store, on an in-memory file system.</summary>
[ExcludeFromCodeCoverage]
public abstract class DocumentTestBase : IDisposable
{
    protected const string Folder = @"C:\Project\Content";

    private bool disposed;

    protected DocumentTestBase()
    {
        this.FileSystem = new MockFileSystem();
        _ = this.FileSystem.Directory.CreateDirectory(Folder);
        this.Store = new NativeAtomicFileStore(this.FileSystem);
        this.Kind = new NoteKind();
        this.Documents = new OpenDocuments([this.Kind], this.Store);
    }

    public TestContext TestContext { get; set; } = null!;

    internal NoteKind Kind { get; }

    protected MockFileSystem FileSystem { get; }

    protected NativeAtomicFileStore Store { get; }

    protected OpenDocuments Documents { get; }

    protected CancellationToken Token => this.TestContext.CancellationToken;

    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    protected static string PathOf(string name) => Path.Combine(Folder, name + NoteKind.Suffix);

    protected static Uri UriOf(string name) => new($"asset:///Content/{name}{NoteKind.Suffix}");

    protected static DocumentFile FileOf(string name) => new(PathOf(name), UriOf(name));

    protected void WriteNote(string name, string text, params Uri[] references)
        => this.FileSystem.File.WriteAllBytes(PathOf(name), NoteKind.Encode(UriOf(name), text, references));

    protected string ReadText(string name) => this.FileSystem.File.ReadAllText(PathOf(name), Encoding.UTF8).Split('\n')[1];

    internal async Task<(Document Document, NoteContent Note)> OpenNoteAsync(string name, string text = "first", params Uri[] references)
    {
        this.WriteNote(name, text, references);
        var result = await this.Documents.OpenAsync(FileOf(name), this.Token).ConfigureAwait(false);
        _ = result.Document.Should().NotBeNull(result.Problem);
        var document = result.Document!;
        var note = (NoteContent)document.Content;
        note.History = document.History;
        return (document, note);
    }

    protected virtual void Dispose(bool disposing)
    {
        if (this.disposed)
        {
            return;
        }

        if (disposing)
        {
            this.Documents.Dispose();
        }

        this.disposed = true;
    }
}
