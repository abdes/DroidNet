// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text;
using DroidNet.Storage;
using Oxygen.Editor.Documents;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Shell.Unit.Tests.Documents;

/// <summary>A minimal document kind for the Shell's tests: a text note held in memory.</summary>
internal sealed class Notes : IDisposable
{
    public Notes() => this.Documents = new OpenDocuments([this.Kind], this.Files);

    public NoteKind Kind { get; } = new();

    public MemoryStore Files { get; } = new();

    public OpenDocuments Documents { get; }

    /// <summary>Gets where the notes' files are: every note asset below <c>/Content</c> is a file of the notes folder.</summary>
    public IDocumentFiles Locations { get; } = new NoteLocations();

    public static AssetPath AssetOf(string name) => AssetPath.Parse($"/Content/{name}.note");

    public static FileSystemPath PathOf(string name) => FileSystemPath.From(Path.Combine(Path.GetTempPath(), "notes", name + ".note"));

    public Task WriteAsync(string name, string text, CancellationToken cancellationToken)
        => this.Files.WriteAsync(PathOf(name).FullPath, Encoding.UTF8.GetBytes(text), FileVersion.Missing, cancellationToken);

    public async Task<Document> OpenAsync(string name, CancellationToken cancellationToken)
    {
        await this.WriteAsync(name, name, cancellationToken).ConfigureAwait(false);
        var opened = await this.Documents.OpenAsync(new DocumentFile(PathOf(name), AssetOf(name)), cancellationToken).ConfigureAwait(false);
        return opened.Document ?? throw new InvalidOperationException(opened.Problem);
    }

    public void Dispose() => this.Documents.Dispose();

    internal sealed class NoteLocations : IDocumentFiles
    {
        public DocumentFile? Find(AssetPath asset)
            => asset.VirtualPath.StartsWith("/Content/", StringComparison.Ordinal) && asset.Name.EndsWith(".note", StringComparison.Ordinal)
                ? new DocumentFile(PathOf(asset.Name[..^".note".Length]), asset)
                : null;

        public DocumentFile? Find(FileSystemPath path)
            => path.Name.EndsWith(".note", StringComparison.Ordinal)
                ? new DocumentFile(path, AssetOf(path.Name[..^".note".Length]))
                : null;
    }

    internal sealed class NoteKind : IDocumentKind
    {
        public string Name => "Note";

        public IReadOnlyList<string> FileSuffixes { get; } = [".note"];

        public Task<DocumentDecodeResult> DecodeAsync(DocumentFile file, ReadOnlyMemory<byte> content, DocumentDecodeContext context, CancellationToken cancellationToken)
            => Task.FromResult(DocumentDecodeResult.Decoded(new NoteContent()));
    }

    internal sealed class NoteContent : IDocumentContent
    {
        /// <summary>Gets or sets whether text is being typed, which finishing the edits ends.</summary>
        public bool Pending { get; set; }

        public bool HasEditsInProgress => this.Pending;

        public Task FinishEditsAsync(CancellationToken cancellationToken)
        {
            this.Pending = false;
            return Task.CompletedTask;
        }

        public DocumentCapture Capture() => DocumentCapture.Of(_ => Task.FromResult<ReadOnlyMemory<byte>>(Encoding.UTF8.GetBytes("saved")));

        public DocumentCapture CaptureCopy(DocumentFile copy) => this.Capture();

        public void ApplyRelocation(DocumentRelocation relocation)
        {
        }
    }

    internal sealed class MemoryStore : IAtomicFileStore
    {
        private readonly ConcurrentDictionary<string, byte[]> files = new(StringComparer.OrdinalIgnoreCase);

        public Task<FileSnapshot> ReadAsync(string path, CancellationToken cancellationToken = default)
            => Task.FromResult(this.files.TryGetValue(path, out var content)
                ? new FileSnapshot([.. content], VersionOf(content))
                : new FileSnapshot(ImmutableArray<byte>.Empty, FileVersion.Missing));

        public Task<FileVersion> WriteAsync(string path, ReadOnlyMemory<byte> content, FileVersion expected, CancellationToken cancellationToken = default)
        {
            var bytes = content.ToArray();
            this.files[path] = bytes;
            return Task.FromResult(VersionOf(bytes));
        }

        public void Delete(string path) => _ = this.files.TryRemove(path, out _);

        private static FileVersion VersionOf(byte[] content) => new(Exists: true, Convert.ToHexString(SHA256.HashData(content)));
    }
}
